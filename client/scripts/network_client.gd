class_name NetworkClient
extends Node

signal status_changed(status: String)
signal rtt_updated(rtt_ms: float)

const MAGIC := 0x424F
const VERSION := 1
const HEADER_SIZE := 10

enum MessageType {
	HELLO = 1,
	HELLO_ACK = 2,
	PING = 3,
	PONG = 4,
}

@export var server_host := "127.0.0.1"
@export var server_port := 39000

var _socket := PacketPeerUDP.new()
var _next_sequence := 1
var _hello_nonce := 0
var _client_id := 0
var _hello_elapsed := 0.0
var _ping_elapsed := 0.0
var _handshake_complete := false


func start() -> void:
	_socket.close()
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


func _process(delta: float) -> void:
	_receive_packets()

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


func _send_packet(message_type: MessageType, payload: PackedByteArray) -> void:
	if payload.size() > 1_190:
		status_changed.emit("Server: packet too large")
		return

	var packet := PackedByteArray()
	_append_u16(packet, MAGIC)
	packet.append(VERSION)
	packet.append(message_type)
	_append_u32(packet, _next_sequence)
	_append_u16(packet, payload.size())
	packet.append_array(payload)
	# 上行序号只标记本客户端发送顺序；v1 不会据此重传或确认。
	_next_sequence += 1

	var send_error := _socket.put_packet(packet)
	if send_error != OK:
		status_changed.emit("Server: send failed (%s)" % error_string(send_error))


func _receive_packets() -> void:
	while _socket.get_available_packet_count() > 0:
		var packet := _socket.get_packet()
		if _socket.get_packet_error() != OK:
			continue
		_handle_packet(packet)


func _handle_packet(packet: PackedByteArray) -> void:
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


func _handle_hello_ack(packet: PackedByteArray, payload_length: int) -> void:
	if payload_length != 8:
		return
	if _read_u32(packet, HEADER_SIZE) != _hello_nonce:
		return

	# nonce 必须匹配当前握手，避免旧 UDP 响应把重连后的客户端误判为已连接。
	_client_id = _read_u32(packet, HEADER_SIZE + 4)
	_handshake_complete = true
	_ping_elapsed = 1.0
	status_changed.emit("Server: connected (client %d)" % _client_id)


func _handle_pong(packet: PackedByteArray, payload_length: int) -> void:
	if not _handshake_complete or payload_length != 8:
		return

	var sent_at_usec := _read_u64(packet, HEADER_SIZE)
	# Pong 回显的是客户端原始时间戳，因此不依赖客户端与服务端时钟同步。
	var rtt_ms := float(Time.get_ticks_usec() - sent_at_usec) / 1_000.0
	if rtt_ms >= 0.0:
		rtt_updated.emit(rtt_ms)


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
