extends Node2D

const TARGET_TICK_RATE := 30

@onready var network_client: NetworkClient = $NetworkClient
@onready var world_view: WorldView = $WorldView
@onready var server_status_label: Label = $StatusLayer/StatusPanel/MarginContainer/StatusList/ServerStatus
@onready var rtt_status_label: Label = $StatusLayer/StatusPanel/MarginContainer/StatusList/RttStatus
@onready var world_status_label: Label = $StatusLayer/StatusPanel/MarginContainer/StatusList/WorldStatus
@onready var simulation_status_label: Label = $StatusLayer/StatusPanel/MarginContainer/StatusList/SimulationStatus


func _ready() -> void:
	server_status_label.text = "Server: offline"
	rtt_status_label.text = "RTT: waiting for connection"
	world_status_label.text = "World: waiting for snapshot"
	simulation_status_label.text = "Simulation: %d Hz target" % TARGET_TICK_RATE
	network_client.status_changed.connect(_on_server_status_changed)
	network_client.rtt_updated.connect(_on_rtt_updated)
	network_client.world_snapshot_received.connect(_on_world_snapshot_received)
	network_client.start()
	_update_world_view_position()


func _process(_delta: float) -> void:
	var move_x := int(Input.is_action_pressed("ui_right")) - int(Input.is_action_pressed("ui_left"))
	var move_y := int(Input.is_action_pressed("ui_down")) - int(Input.is_action_pressed("ui_up"))
	network_client.set_movement(move_x, move_y)
	_update_world_view_position()


func _on_server_status_changed(status: String) -> void:
	server_status_label.text = status


func _on_rtt_updated(rtt_ms: float) -> void:
	rtt_status_label.text = "RTT: %.2f ms" % rtt_ms


func _on_world_snapshot_received(server_tick: int, _acknowledged_input_sequence: int, entities: Array) -> void:
	world_view.apply_snapshot(entities, network_client.get_client_id())
	world_status_label.text = "World: tick %d, players %d" % [server_tick, entities.size()]


func _update_world_view_position() -> void:
	world_view.position = get_viewport_rect().size * 0.5
