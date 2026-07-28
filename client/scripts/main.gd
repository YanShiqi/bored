extends Node2D

const TARGET_TICK_RATE := 30

@onready var network_client: NetworkClient = $NetworkClient
@onready var world_view: WorldView = $WorldView
@onready var server_status_label: Label = $StatusLayer/StatusPanel/MarginContainer/StatusList/ServerStatus
@onready var rtt_status_label: Label = $StatusLayer/StatusPanel/MarginContainer/StatusList/RttStatus
@onready var world_status_label: Label = $StatusLayer/StatusPanel/MarginContainer/StatusList/WorldStatus
@onready var simulation_status_label: Label = $StatusLayer/StatusPanel/MarginContainer/StatusList/SimulationStatus
@onready var snapshot_status_label: Label = $StatusLayer/StatusPanel/MarginContainer/StatusList/SnapshotStatus
@onready var prediction_status_label: Label = $StatusLayer/StatusPanel/MarginContainer/StatusList/PredictionStatus
@onready var network_simulation_status_label: Label = $StatusLayer/StatusPanel/MarginContainer/StatusList/NetworkSimulationStatus

var _local_prediction := LocalPrediction.new()
var _prediction_enabled := true


func _ready() -> void:
	server_status_label.text = "Server: offline"
	rtt_status_label.text = "RTT: waiting for connection"
	world_status_label.text = "World: waiting for snapshot"
	simulation_status_label.text = "Simulation: %d Hz target" % TARGET_TICK_RATE
	snapshot_status_label.text = "Snapshots: waiting"
	prediction_status_label.text = "Prediction: waiting"
	_configure_experiment_from_command_line()
	network_client.status_changed.connect(_on_server_status_changed)
	network_client.rtt_updated.connect(_on_rtt_updated)
	network_client.input_command_sent.connect(_on_input_command_sent)
	network_client.world_snapshot_received.connect(_on_world_snapshot_received)
	network_client.start()
	_update_world_view_position()


func _process(_delta: float) -> void:
	var move_x := int(Input.is_action_pressed("ui_right")) - int(Input.is_action_pressed("ui_left"))
	var move_y := int(Input.is_action_pressed("ui_down")) - int(Input.is_action_pressed("ui_up"))
	network_client.set_movement(move_x, move_y)
	_update_world_view_position()
	_update_diagnostics()


func _on_server_status_changed(status: String) -> void:
	server_status_label.text = status


func _on_rtt_updated(rtt_ms: float) -> void:
	rtt_status_label.text = "RTT: %.2f ms" % rtt_ms


func _on_input_command_sent(
	client_tick: int,
	input_sequence: int,
	move_x: int,
	move_y: int
) -> void:
	if not _prediction_enabled:
		return
	if _local_prediction.record_sent_input(client_tick, input_sequence, move_x, move_y):
		world_view.set_local_predicted_position(_local_prediction.get_predicted_position_mm())


func _on_world_snapshot_received(
	server_tick: int,
	acknowledged_input_sequence: int,
	entities: Array,
	received_at_usec: int
) -> void:
	var local_client_id := network_client.get_client_id()
	if not world_view.apply_snapshot(server_tick, received_at_usec, entities, local_client_id):
		return

	for entity in entities:
		if int(entity["client_id"]) != local_client_id:
			continue

		var predicted_before_reconcile := _local_prediction.get_predicted_position_mm()
		var correction_count_before := _local_prediction.get_correction_count()
		var authoritative_position_mm := Vector2i(
			int(entity["position_x_mm"]),
			int(entity["position_y_mm"])
		)
		if not _prediction_enabled:
			# 对照模式直接显示最近权威位置，可用于观察 15 Hz 快照造成的本地阶梯感。
			world_view.set_local_predicted_position(authoritative_position_mm)
			break

		if _local_prediction.reconcile(
			server_tick,
			authoritative_position_mm,
			acknowledged_input_sequence
		):
			var reconciled_position_mm := _local_prediction.get_predicted_position_mm()
			world_view.set_local_predicted_position(reconciled_position_mm)
			if _local_prediction.get_correction_count() > correction_count_before:
				# 红线只展示本次视觉校正的起止点，不参与后续预测或碰撞计算。
				world_view.show_prediction_correction(
					predicted_before_reconcile,
					reconciled_position_mm
				)
		break

	world_status_label.text = "World: tick %d, players %d" % [server_tick, entities.size()]


func _configure_experiment_from_command_line() -> void:
	var latency_ms := network_client.simulated_latency_ms
	var jitter_ms := network_client.simulated_jitter_ms
	var packet_loss_percent := network_client.simulated_packet_loss_percent
	var random_seed := network_client.simulated_random_seed

	# Godot 自定义参数放在命令行的 `--` 之后，例如：
	# godot --path client -- --latency-ms=100 --jitter-ms=20 --loss-percent=5
	# `--disable-prediction` 与 `--interpolation-ms=0` 用于和阶段 2 的直接快照显示作对照。
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--latency-ms="):
			latency_ms = int(argument.trim_prefix("--latency-ms="))
		elif argument.begins_with("--jitter-ms="):
			jitter_ms = int(argument.trim_prefix("--jitter-ms="))
		elif argument.begins_with("--loss-percent="):
			packet_loss_percent = float(argument.trim_prefix("--loss-percent="))
		elif argument.begins_with("--seed="):
			random_seed = int(argument.trim_prefix("--seed="))
		elif argument.begins_with("--interpolation-ms="):
			world_view.interpolation_delay_ms = clampi(
				int(argument.trim_prefix("--interpolation-ms=")),
				0,
				500
			)
		elif argument == "--disable-prediction":
			_prediction_enabled = false

	network_client.configure_network_simulation(
		latency_ms,
		jitter_ms,
		packet_loss_percent,
		random_seed
	)
	network_simulation_status_label.text = network_client.get_network_simulation_summary()
	simulation_status_label.text = "Simulation: %d Hz, prediction %s" % [
		TARGET_TICK_RATE,
		"on" if _prediction_enabled else "off",
	]


func _update_diagnostics() -> void:
	var snapshot_age_ms := world_view.get_latest_snapshot_age_ms()
	if snapshot_age_ms < 0.0:
		snapshot_status_label.text = "Snapshots: waiting"
	else:
		snapshot_status_label.text = "Snapshots: age %.1f ms, interpolation %d ms, remotes %d" % [
			snapshot_age_ms,
			world_view.interpolation_delay_ms,
			world_view.get_remote_entity_count(),
		]

	if _prediction_enabled:
		prediction_status_label.text = "Prediction: pending %d, error %.1f mm, corrections %d, dropped %d" % [
			_local_prediction.get_pending_input_count(),
			_local_prediction.get_last_prediction_error_mm(),
			_local_prediction.get_correction_count(),
			_local_prediction.get_dropped_pending_input_count(),
		]
	else:
		prediction_status_label.text = "Prediction: disabled (latest authority snapshot)"
	network_simulation_status_label.text = network_client.get_network_simulation_summary()


func _update_world_view_position() -> void:
	world_view.position = get_viewport_rect().size * 0.5
