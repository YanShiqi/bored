class_name WorldView
extends Node2D

const PIXELS_PER_METER := 20.0
const HALF_WIDTH := 220.0
const HALF_HEIGHT := 160.0
const CORRECTION_LINE_DURATION_SECONDS := 0.35

@export_range(0, 500, 1) var interpolation_delay_ms := 100

var _local_client_id := 0
var _local_position_mm := Vector2.ZERO
var _has_local_position := false

# 每个远端 client_id 拥有独立缓冲。网络快照更新模拟样本，_process 则按渲染时间连续采样，
# 两条时间轴分离后，15 Hz 的离散网络更新不会直接变成屏幕上的 15 次跳动。
var _remote_snapshot_buffers: Dictionary = {}
var _remote_render_positions_mm: Dictionary = {}

var _last_snapshot_server_tick := -1
var _latest_snapshot_received_at_usec := -1
var _correction_from_mm := Vector2.ZERO
var _correction_to_mm := Vector2.ZERO
var _correction_line_remaining_seconds := 0.0


func apply_snapshot(
	server_tick: int,
	received_at_usec: int,
	entities: Array,
	local_client_id: int
) -> bool:
	# 在世界视图入口整包过滤旧 Tick。这样实体、玩家列表、快照年龄和 UI 中的世界 Tick
	# 都来自同一份最新快照，不会出现“位置没倒退但诊断时间被旧包刷新”的矛盾状态。
	if server_tick <= _last_snapshot_server_tick:
		return false

	_last_snapshot_server_tick = server_tick
	_local_client_id = local_client_id
	_latest_snapshot_received_at_usec = received_at_usec

	# WorldSnapshot 是完整世界列表，因此本帧未出现的远端实体可直接移除；
	# 本地实体不进入插值缓冲，它由 LocalPrediction 单独维护。
	var present_remote_client_ids: Dictionary = {}
	for entity in entities:
		var client_id := int(entity["client_id"])
		if client_id == _local_client_id:
			continue

		present_remote_client_ids[client_id] = true
		if not _remote_snapshot_buffers.has(client_id):
			_remote_snapshot_buffers[client_id] = SnapshotBuffer.new()

		var buffer: SnapshotBuffer = _remote_snapshot_buffers[client_id]
		buffer.push_snapshot(
			server_tick,
			received_at_usec,
			Vector2(float(entity["position_x_mm"]), float(entity["position_y_mm"]))
		)

	for existing_client_id in _remote_snapshot_buffers.keys():
		if not present_remote_client_ids.has(existing_client_id):
			_remote_snapshot_buffers.erase(existing_client_id)
			_remote_render_positions_mm.erase(existing_client_id)
	return true


func set_local_predicted_position(position_mm: Vector2i) -> void:
	_local_position_mm = Vector2(position_mm)
	_has_local_position = true


func show_prediction_correction(from_position_mm: Vector2i, to_position_mm: Vector2i) -> void:
	_correction_from_mm = Vector2(from_position_mm)
	_correction_to_mm = Vector2(to_position_mm)
	_correction_line_remaining_seconds = CORRECTION_LINE_DURATION_SECONDS


func get_latest_snapshot_age_ms() -> float:
	if _latest_snapshot_received_at_usec < 0:
		return -1.0
	return maxf(
		0.0,
		float(Time.get_ticks_usec() - _latest_snapshot_received_at_usec) / 1_000.0
	)


func get_remote_entity_count() -> int:
	return _remote_snapshot_buffers.size()


func _process(delta: float) -> void:
	var render_time_usec := Time.get_ticks_usec() - interpolation_delay_ms * 1_000
	for client_id in _remote_snapshot_buffers:
		var buffer: SnapshotBuffer = _remote_snapshot_buffers[client_id]
		_remote_render_positions_mm[client_id] = buffer.sample_position(render_time_usec)

	if _correction_line_remaining_seconds > 0.0:
		_correction_line_remaining_seconds = maxf(
			0.0,
			_correction_line_remaining_seconds - delta
		)
	queue_redraw()


func _draw() -> void:
	var bounds := Rect2(-HALF_WIDTH, -HALF_HEIGHT, HALF_WIDTH * 2.0, HALF_HEIGHT * 2.0)
	draw_rect(bounds, Color("15232b"), true)
	draw_rect(bounds, Color("6d8794"), false, 2.0)

	for offset in range(-200, 201, 40):
		draw_line(Vector2(offset, -HALF_HEIGHT), Vector2(offset, HALF_HEIGHT), Color("233a45"), 1.0)
		draw_line(Vector2(-HALF_WIDTH, offset), Vector2(HALF_WIDTH, offset), Color("233a45"), 1.0)

	draw_line(Vector2(-HALF_WIDTH, 0), Vector2(HALF_WIDTH, 0), Color("3d6474"), 1.0)
	draw_line(Vector2(0, -HALF_HEIGHT), Vector2(0, HALF_HEIGHT), Color("3d6474"), 1.0)

	if _correction_line_remaining_seconds > 0.0:
		var correction_alpha := (
			_correction_line_remaining_seconds / CORRECTION_LINE_DURATION_SECONDS
		)
		draw_line(
			_to_screen_position(_correction_from_mm),
			_to_screen_position(_correction_to_mm),
			Color(1.0, 0.35, 0.25, correction_alpha),
			3.0
		)

	for client_id in _remote_render_positions_mm:
		var remote_position_mm := _remote_render_positions_mm[client_id] as Vector2
		_draw_player(remote_position_mm, Color("8bd450"))

	if _has_local_position:
		_draw_player(_local_position_mm, Color("5bc0eb"))


func _draw_player(position_mm: Vector2, player_color: Color) -> void:
	var screen_position := _to_screen_position(position_mm)
	draw_circle(screen_position, 12.0, player_color)
	draw_circle(screen_position, 5.0, Color("102026"))


func _to_screen_position(position_mm: Vector2) -> Vector2:
	return position_mm / 1_000.0 * PIXELS_PER_METER
