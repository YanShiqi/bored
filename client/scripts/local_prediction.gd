class_name LocalPrediction
extends RefCounted

const MOVEMENT_PER_TICK_MM := 100
const WORLD_BOUNDARY_MM := 9_000
const MAX_PENDING_INPUTS := 256

# 这是客户端为了即时显示而维护的模拟位置，不是可以提交给服务端的权威位置。
var _predicted_position_mm := Vector2i.ZERO

# 每条已发送但尚未被快照确认的输入都必须保留。收到权威位置后，会按序重放这些输入，
# 从而把服务端已经处理的历史与客户端仍在等待处理的未来重新接到同一条时间线上。
var _pending_inputs: Array[Dictionary] = []

var _last_reconciled_server_tick := -1
var _last_acknowledged_input_sequence := 0
var _has_authoritative_state := false

# 误差表示“校正前预测位置”到“权威位置加未确认输入重放结果”的距离，
# 它对应玩家实际可能看到的跳变幅度，而不是简单比较预测位置和较旧快照位置。
var _last_prediction_error_mm := 0.0
var _max_prediction_error_mm := 0.0
var _correction_count := 0
var _dropped_pending_input_count := 0


func reset() -> void:
	_predicted_position_mm = Vector2i.ZERO
	_pending_inputs.clear()
	_last_reconciled_server_tick = -1
	_last_acknowledged_input_sequence = 0
	_has_authoritative_state = false
	_last_prediction_error_mm = 0.0
	_max_prediction_error_mm = 0.0
	_correction_count = 0
	_dropped_pending_input_count = 0


func record_sent_input(client_tick: int, input_sequence: int, move_x: int, move_y: int) -> bool:
	if move_x < -1 or move_x > 1 or move_y < -1 or move_y > 1:
		return false
	if not _pending_inputs.is_empty():
		var newest_sequence := int(_pending_inputs.back()["input_sequence"])
		if input_sequence <= newest_sequence:
			return false

	_pending_inputs.append({
		"client_tick": client_tick,
		"input_sequence": input_sequence,
		"move_x": move_x,
		"move_y": move_y,
	})

	# 客户端在输入真正交给 UDP 层后立刻推进一个固定步长，不等待下一份 15 Hz 世界快照。
	# 这里使用整数毫米和逐轴裁剪，刻意与 C++ AuthoritativeWorld 的计算顺序保持一致。
	_predicted_position_mm = _simulate_input(_predicted_position_mm, move_x, move_y)

	if _pending_inputs.size() > MAX_PENDING_INPUTS:
		# 长时间收不到确认时必须保持内存有界。丢弃最老输入会降低下一次重放精度，
		# 因此单独统计，让诊断面板能够暴露连接已经不适合继续预测。
		_pending_inputs.pop_front()
		_dropped_pending_input_count += 1
	return true


func reconcile(
	server_tick: int,
	authoritative_position_mm: Vector2i,
	acknowledged_input_sequence: int
) -> bool:
	# 快照也走 UDP。旧 Tick 的权威位置不得覆盖已经用更新快照完成的校正。
	if server_tick <= _last_reconciled_server_tick:
		return false

	var predicted_before_reconcile := _predicted_position_mm
	_last_reconciled_server_tick = server_tick
	_last_acknowledged_input_sequence = max(
		_last_acknowledged_input_sequence,
		acknowledged_input_sequence
	)

	# ack 表示这些输入已经进入服务端状态。它们的结果已包含在 authoritative_position_mm 中，
	# 若再次重放就会把同一次移动计算两遍，所以先从队首删除。
	while not _pending_inputs.is_empty():
		var oldest_sequence := int(_pending_inputs.front()["input_sequence"])
		if oldest_sequence > _last_acknowledged_input_sequence:
			break
		_pending_inputs.pop_front()

	# 每次都先无条件回到权威位置，再按发送顺序重放服务端尚未确认的输入。
	# 这个过程只修复客户端的预测副本，不会向服务端发送位置或覆盖服务端状态。
	_predicted_position_mm = authoritative_position_mm
	for pending_input in _pending_inputs:
		_predicted_position_mm = _simulate_input(
			_predicted_position_mm,
			int(pending_input["move_x"]),
			int(pending_input["move_y"])
		)

	var correction_delta := Vector2(predicted_before_reconcile - _predicted_position_mm)
	var correction_distance_mm := correction_delta.length()
	if _has_authoritative_state:
		_last_prediction_error_mm = correction_distance_mm
		_max_prediction_error_mm = maxf(_max_prediction_error_mm, correction_distance_mm)
		if correction_distance_mm > 0.0:
			_correction_count += 1
	else:
		# 第一份快照用于建立客户端与服务端的共同起点，不把初始化位置算作一次网络校正。
		_has_authoritative_state = true
		_last_prediction_error_mm = 0.0
	return true


func _simulate_input(position_mm: Vector2i, move_x: int, move_y: int) -> Vector2i:
	# 阶段 3 延续服务端当前“逐轴移动”的规则：对角线不会归一化，因此对角速度更快。
	# 将来若修改该规则，必须同时改 C++、这里和 shared/protocol 中的跨端契约。
	return Vector2i(
		clampi(position_mm.x + move_x * MOVEMENT_PER_TICK_MM, -WORLD_BOUNDARY_MM, WORLD_BOUNDARY_MM),
		clampi(position_mm.y + move_y * MOVEMENT_PER_TICK_MM, -WORLD_BOUNDARY_MM, WORLD_BOUNDARY_MM)
	)


func get_predicted_position_mm() -> Vector2i:
	return _predicted_position_mm


func get_pending_input_count() -> int:
	return _pending_inputs.size()


func get_last_prediction_error_mm() -> float:
	return _last_prediction_error_mm


func get_max_prediction_error_mm() -> float:
	return _max_prediction_error_mm


func get_correction_count() -> int:
	return _correction_count


func get_dropped_pending_input_count() -> int:
	return _dropped_pending_input_count


func get_last_acknowledged_input_sequence() -> int:
	return _last_acknowledged_input_sequence
