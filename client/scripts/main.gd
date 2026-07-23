extends Node2D

const TARGET_TICK_RATE := 30

@onready var network_client = $NetworkClient
@onready var server_status_label: Label = $StatusLayer/StatusPanel/MarginContainer/StatusList/ServerStatus
@onready var rtt_status_label: Label = $StatusLayer/StatusPanel/MarginContainer/StatusList/RttStatus
@onready var simulation_status_label: Label = $StatusLayer/StatusPanel/MarginContainer/StatusList/SimulationStatus


func _ready() -> void:
	server_status_label.text = "Server: offline"
	rtt_status_label.text = "RTT: waiting for connection"
	simulation_status_label.text = "Simulation: %d Hz target" % TARGET_TICK_RATE
	network_client.status_changed.connect(_on_server_status_changed)
	network_client.rtt_updated.connect(_on_rtt_updated)
	network_client.start()


func _on_server_status_changed(status: String) -> void:
	server_status_label.text = status


func _on_rtt_updated(rtt_ms: float) -> void:
	rtt_status_label.text = "RTT: %.2f ms" % rtt_ms
