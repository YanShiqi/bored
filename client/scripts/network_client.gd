class_name NetworkClient
extends Node

signal status_changed(status: String)
signal rtt_updated(rtt_ms: float)
signal input_command_sent(client_tick: int, input_sequence: int, move_x: int, move_y: int)
signal world_snapshot_received(
	server_tick: int,
	acknowledged_input_sequence: int,
	entities: Array,
	received_at_usec: int
)

const MAGIC := 0x424F
const VERSION := 2
const HEADER_SIZE := 10
const INPUT_COMMAND_SIZE := 10
const WORLD_SNAPSHOT_PREFIX_SIZE := 10
const SNAPSHOT_ENTITY_SIZE := 12
const MAX_SNAPSHOT_ENTITIES := 98
const INPUT_INTERVAL_SECONDS := 1.0 / 30.0

enum MessageType {
	HELLO = 1,
	HELLO_ACK = 2,
	PING = 3,
	PONG = 4,
	INPUT_COMMAND = 5,
	WORLD_SNAPSHOT = 6,
}

@export var server_host := "127.0.0.1"
@export var server_port := 39000
@export_range(0, 2_000, 1) var simulated_latency_ms := 0
@export_range(0, 1_000, 1) var simulated_jitter_ms := 0
@export_range(0.0, 100.0, 0.1) var simulated_packet_loss_percent := 0.0
@export var simulated_random_seed := 1_337

var _socket := PacketPeerUDP.new()
var _random := RandomNumberGenerator.new()
var _next_sequence := 1
var _hello_nonce := 0
var _client_id := 0
var _hello_elapsed := 0.0
var _ping_elapsed := 0.0
var _input_elapsed := 0.0
var _client_tick := 0
var _input_sequence := 1
var _move_x := 0
var _move_y := 0
var _handshake_complete := false

# 网络劣化器位于协议编解码与真实 UDP 套接字之间。队列中的包仍是完整二进制数据，
# 因此延迟、抖动、丢包和乱序会经过与真实网络相同的消息解析路径。
var _delayed_outgoing_packets: Array[Dictionary] = []
var _delayed_incoming_packets: Array[Dictionary] = []
var _simulated_dropped_outgoing_packets := 0
var _simulated_dropped_incoming_packets := 0


func start() -> void:
	_socket.close()
	_delayed_outgoing_packets.clear()
	_delayed_incoming_packets.clear()
	_handshake_complete = false
	_client_id = 0
	_hello_elapsed = 0.0
	_ping_elapsed = 0.0
	_input_elapsed = 0.0
	_simulated_dropped_outgoing_packets = 0
	_simulated_dropped_incoming_packets = 0
	_random.seed = simulated_random_seed
	# 绑定临时本地端口；服务端根据这个端口回复同一条 UDP 会话。
	var bind_error := _socket.bind(0)
	if bind_error != OK:
		status_changed.emit("Server: UDP bind failed (%s)" % error_string(bind_error))
		return

	_socket.connect_to_host(server_host, server_port)
	# nonce 用于把当前连接尝试与延迟到达的旧 HelloAck 区分开。
	_hello_nonce = int(Time.get_ticks_usec() % 4_294_967_295)
	status_changed.emit("Server: connecting to %s:%d" % [server_host, server_port])
	_send_hello()


func set_movement(move_x: int, move_y: int) -> void:
	_move_x = clampi(move_x, -1, 1)
	_move_y = clampi(move_y, -1, 1)


func get_client_id() -> int:
	return _client_id


func configure_network_simulation(
	latency_ms: int,
	jitter_ms: int,
	packet_loss_percent: float,
	random_seed: int
) -> void:
	# latency 是每个方向的单程延迟；Ping 同时经过上行和下行，所以 RTT 会增加约两倍 latency。
	simulated_latency_ms = clampi(latency_ms, 0, 2_000)
	simulated_jitter_ms = clampi(jitter_ms, 0, 1_000)
	simulated_packet_loss_percent = clampf(packet_loss_percent, 0.0, 100.0)
	simulated_random_seed = random_seed
	_random.seed = simulated_random_seed


func get_network_simulation_summary() -> String:
	return "Net sim: %d ms one-way, +/- %d ms, %.1f%% loss, dropped %d/%d" % [
		simulated_latency_ms,
		simulated_jitter_ms,
		simulated_packet_loss_percent,
		_simulated_dropped_outgoing_packets,
		_simulated_dropped_incoming_packets,
	]


func _process(delta: float) -> void:
	_flush_delayed_outgoing_packets()
	_receive_packets()
	_flush_delayed_incoming_packets()

	if not _socket.is_bound():
		return

	if not _handshake_complete:
		# UDP 不保证到达，因此握手完成前周期性重发 Hello。
		_hello_elapsed += delta
		if _hello_elapsed >= 1.0:
			_send_hello()
		return

	_ping_elapsed += delta
	if _ping_elapsed >= 1.0:
		_send_ping()

	_input_elapsed += delta
	while _input_elapsed >= INPUT_INTERVAL_SECONDS:
		_input_elapsed -= INPUT_INTERVAL_SECONDS
		_send_input_command()


func _exit_tree() -> void:
	_socket.close()


func _send_hello() -> void:
	_hello_elapsed = 0.0
	var payload := PackedByteArray()
	_append_u32(payload, _hello_nonce)
	_send_packet(MessageType.HELLO, payload)


func _send_ping() -> void:
	_ping_elapsed = 0.0
	var payload := PackedByteArray()
	_append_u64(payload, Time.get_ticks_usec())
	_send_packet(MessageType.PING, payload)


func _send_input_command() -> void:
	var sent_client_tick := _client_tick
	var sent_input_sequence := _input_sequence
	var payload := PackedByteArray()
	_append_u32(payload, sent_client_tick)
	_append_u32(payload, sent_input_sequence)
	_append_i8(payload, _move_x)
	_append_i8(payload, _move_y)
	_client_tick += 1
	_input_sequence += 1
	if _send_packet(MessageType.INPUT_COMMAND, payload):
		# 即使劣化器随后模拟丢包，客户端也像面对真实 UDP 一样认为发送成功并立即预测；
		# 服务端快照中的 ack 会在之后暴露丢失并触发校正。
		input_command_sent.emit(sent_client_tick, sent_input_sequence, _move_x, _move_y)


func _send_packet(message_type: MessageType, payload: PackedByteArray) -> bool:
	if payload.size() > 1_190:
		status_changed.emit("Server: packet too large")
		return false

	var packet := PackedByteArray()
	_append_u16(packet, MAGIC)
	packet.append(VERSION)
	packet.append(message_type)
	_append_u32(packet, _next_sequence)
	_append_u16(packet, payload.size())
	packet.append_array(payload)
	# 上行序号只标记本客户端发送顺序；v2 不会据此重传或确认。
	_next_sequence += 1

	if _should_simulate_packet_loss():
		_simulated_dropped_outgoing_packets += 1
		return true

	var delay_usec := _next_simulated_delay_usec()
	if delay_usec > 0:
		_delayed_outgoing_packets.append({
			"deliver_at_usec": Time.get_ticks_usec() + delay_usec,
			"packet": packet,
		})
		return true

	return _put_packet(packet)


func _put_packet(packet: PackedByteArray) -> bool:
	var send_error := _socket.put_packet(packet)
	if send_error != OK:
		status_changed.emit("Server: send failed (%s)" % error_string(send_error))
		return false
	return true


func _receive_packets() -> void:
	while _socket.get_available_packet_count() > 0:
		var packet := _socket.get_packet()
		if _socket.get_packet_error() != OK:
			continue
		if _should_simulate_packet_loss():
			_simulated_dropped_incoming_packets += 1
			continue

		var delay_usec := _next_simulated_delay_usec()
		if delay_usec > 0:
			_delayed_incoming_packets.append({
				"deliver_at_usec": Time.get_ticks_usec() + delay_usec,
				"packet": packet,
			})
		else:
			_handle_packet(packet, Time.get_ticks_usec())


func _flush_delayed_outgoing_packets() -> void:
	var now_usec := Time.get_ticks_usec()
	var packet_index := 0
	while packet_index < _delayed_outgoing_packets.size():
		var queued_packet: Dictionary = _delayed_outgoing_packets[packet_index]
		if int(queued_packet["deliver_at_usec"]) > now_usec:
			packet_index += 1
			continue
		_put_packet(queued_packet["packet"] as PackedByteArray)
		_delayed_outgoing_packets.remove_at(packet_index)


func _flush_delayed_incoming_packets() -> void:
	var now_usec := Time.get_ticks_usec()
	var packet_index := 0
	while packet_index < _delayed_incoming_packets.size():
		var queued_packet: Dictionary = _delayed_incoming_packets[packet_index]
		if int(queued_packet["deliver_at_usec"]) > now_usec:
			packet_index += 1
			continue
		# 使用“交付给协议层”的时间作为快照接收时间，插值缓冲才能观察到模拟抖动。
		_handle_packet(queued_packet["packet"] as PackedByteArray, now_usec)
		_delayed_incoming_packets.remove_at(packet_index)


func _next_simulated_delay_usec() -> int:
	var jitter_offset_ms := 0.0
	if simulated_jitter_ms > 0:
		jitter_offset_ms = _random.randf_range(
			-float(simulated_jitter_ms),
			float(simulated_jitter_ms)
		)
	var effective_delay_ms := maxf(0.0, float(simulated_latency_ms) + jitter_offset_ms)
	return int(round(effective_delay_ms * 1_000.0))


func _should_simulate_packet_loss() -> bool:
	if simulated_packet_loss_percent <= 0.0:
		return false
	return _random.randf() * 100.0 < simulated_packet_loss_percent


func _handle_packet(packet: PackedByteArray, received_at_usec: int) -> void:
	if packet.size() < HEADER_SIZE:
		return
	if _read_u16(packet, 0) != MAGIC or packet[2] != VERSION:
		return

	var message_type := packet[3]
	var payload_length := _read_u16(packet, 8)
	# 长度精确匹配后才读取消息体，避免畸形 UDP 包进入业务状态机。
	if packet.size() != HEADER_SIZE + payload_length:
		return

	if message_type == MessageType.HELLO_ACK:
		_handle_hello_ack(packet, payload_length)
	elif message_type == MessageType.PONG:
		_handle_pong(packet, payload_length)
	elif message_type == MessageType.WORLD_SNAPSHOT:
		_handle_world_snapshot(packet, payload_length, received_at_usec)


func _handle_hello_ack(packet: PackedByteArray, payload_length: int) -> void:
	if payload_length != 8:
		return
	if _read_u32(packet, HEADER_SIZE) != _hello_nonce:
		return

	# nonce 必须匹配当前握手，避免旧 UDP 响应把重连后的客户端误判为已连接。
	_client_id = _read_u32(packet, HEADER_SIZE + 4)
	_handshake_complete = true
	_ping_elapsed = 1.0
	_input_elapsed = INPUT_INTERVAL_SECONDS
	status_changed.emit("Server: connected (client %d)" % _client_id)


func _handle_pong(packet: PackedByteArray, payload_length: int) -> void:
	if not _handshake_complete or payload_length != 8:
		return

	var sent_at_usec := _read_u64(packet, HEADER_SIZE)
	# Pong 回显的是客户端原始时间戳，因此不依赖客户端与服务端时钟同步。
	var rtt_ms := float(Time.get_ticks_usec() - sent_at_usec) / 1_000.0
	if rtt_ms >= 0.0:
		rtt_updated.emit(rtt_ms)


func _handle_world_snapshot(
	packet: PackedByteArray,
	payload_length: int,
	received_at_usec: int
) -> void:
	if not _handshake_complete or payload_length < WORLD_SNAPSHOT_PREFIX_SIZE:
		return

	var entity_count := _read_u16(packet, HEADER_SIZE + 8)
	if entity_count > MAX_SNAPSHOT_ENTITIES:
		return
	if payload_length != WORLD_SNAPSHOT_PREFIX_SIZE + entity_count * SNAPSHOT_ENTITY_SIZE:
		return

	var server_tick := _read_u32(packet, HEADER_SIZE)
	var acknowledged_input_sequence := _read_u32(packet, HEADER_SIZE + 4)
	var entities: Array = []
	for index in range(entity_count):
		var offset := HEADER_SIZE + WORLD_SNAPSHOT_PREFIX_SIZE + index * SNAPSHOT_ENTITY_SIZE
		entities.append({
			"client_id": _read_u32(packet, offset),
			"position_x_mm": _read_i32(packet, offset + 4),
			"position_y_mm": _read_i32(packet, offset + 8),
		})

	world_snapshot_received.emit(
		server_tick,
		acknowledged_input_sequence,
		entities,
		received_at_usec
	)


# 所有多字节字段手动按大端序编码，与 C++ 端和 protocol.md 保持一致。
func _append_u16(buffer: PackedByteArray, value: int) -> void:
	buffer.append((value >> 8) & 0xFF)
	buffer.append(value & 0xFF)


func _append_u32(buffer: PackedByteArray, value: int) -> void:
	for shift in range(24, -1, -8):
		buffer.append((value >> shift) & 0xFF)


func _append_u64(buffer: PackedByteArray, value: int) -> void:
	for shift in range(56, -1, -8):
		buffer.append((value >> shift) & 0xFF)


func _append_i8(buffer: PackedByteArray, value: int) -> void:
	buffer.append(value & 0xFF)


func _read_u16(buffer: PackedByteArray, offset: int) -> int:
	return (int(buffer[offset]) << 8) | int(buffer[offset + 1])


func _read_u32(buffer: PackedByteArray, offset: int) -> int:
	var value := 0
	for index in range(4):
		value = (value << 8) | int(buffer[offset + index])
	return value


func _read_u64(buffer: PackedByteArray, offset: int) -> int:
	var value := 0
	for index in range(8):
		value = (value << 8) | int(buffer[offset + index])
	return value


func _read_i32(buffer: PackedByteArray, offset: int) -> int:
	var value := _read_u32(buffer, offset)
	if (value & 0x80000000) != 0:
		value -= 0x1_0000_0000
	return value
