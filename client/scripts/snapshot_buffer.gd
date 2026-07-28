class_name SnapshotBuffer
extends RefCounted

const MAX_SAMPLES := 32

# 每个元素都保存同一远端实体在某个服务端 Tick 的位置，以及该快照抵达本机的单调时钟时间。
# 插值只使用本机接收时间，不要求客户端与服务端系统时钟同步。
var _samples: Array[Dictionary] = []
var _last_server_tick := -1


func clear() -> void:
	_samples.clear()
	_last_server_tick = -1


func push_snapshot(server_tick: int, received_at_usec: int, position_mm: Vector2) -> bool:
	# UDP 可能乱序或重复送达。已经观察过的 Tick 不再写入缓冲，避免远端实体回到旧位置。
	# uint32 Tick 回绕需要数年连续运行，当前实验阶段暂不处理回绕比较。
	if server_tick <= _last_server_tick:
		return false

	_last_server_tick = server_tick
	_samples.append({
		"server_tick": server_tick,
		"received_at_usec": received_at_usec,
		"position_mm": position_mm,
	})

	# 15 Hz 快照下 32 个样本约覆盖两秒。固定上限可以防止断点调试或渲染暂停时持续占用内存。
	if _samples.size() > MAX_SAMPLES:
		_samples.pop_front()
	return true


func sample_position(render_time_usec: int) -> Vector2:
	if _samples.is_empty():
		return Vector2.ZERO

	var first_sample: Dictionary = _samples.front()
	if render_time_usec <= int(first_sample["received_at_usec"]):
		return first_sample["position_mm"] as Vector2

	# 找到时间上包围 render_time 的 A、B 两帧。位置只在这两个已确认快照之间线性插值，
	# 因而不会像外推那样在丢包后继续沿错误方向移动。
	for index in range(1, _samples.size()):
		var newer_sample: Dictionary = _samples[index]
		var newer_time := int(newer_sample["received_at_usec"])
		if render_time_usec > newer_time:
			continue

		var older_sample: Dictionary = _samples[index - 1]
		var older_time := int(older_sample["received_at_usec"])
		var interval_usec := newer_time - older_time
		if interval_usec <= 0:
			# 两个包可能在同一次网络轮询中被取出，接收时间相同；此时直接采用较新的 Tick。
			return newer_sample["position_mm"] as Vector2

		var interpolation_ratio := clampf(
			float(render_time_usec - older_time) / float(interval_usec),
			0.0,
			1.0
		)
		var older_position := older_sample["position_mm"] as Vector2
		var newer_position := newer_sample["position_mm"] as Vector2
		return older_position.lerp(newer_position, interpolation_ratio)

	# 缓冲尚未收到目标时间之后的快照时保持最新位置，不对远端实体做未经服务端确认的外推。
	var latest_sample: Dictionary = _samples.back()
	return latest_sample["position_mm"] as Vector2


func get_snapshot_age_ms(now_usec: int) -> float:
	if _samples.is_empty():
		return 0.0
	var latest_sample: Dictionary = _samples.back()
	return maxf(0.0, float(now_usec - int(latest_sample["received_at_usec"])) / 1_000.0)


func get_sample_count() -> int:
	return _samples.size()


func get_last_server_tick() -> int:
	return _last_server_tick
