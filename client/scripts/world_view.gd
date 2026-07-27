class_name WorldView
extends Node2D

const PIXELS_PER_METER := 20.0
const HALF_WIDTH := 220.0
const HALF_HEIGHT := 160.0

var _entities: Array = []
var _local_client_id := 0


func apply_snapshot(entities: Array, local_client_id: int) -> void:
	_entities = entities
	_local_client_id = local_client_id
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

	for entity in _entities:
		var position := Vector2(
			float(entity["position_x_mm"]) / 1_000.0,
			float(entity["position_y_mm"]) / 1_000.0
		) * PIXELS_PER_METER
		var is_local := int(entity["client_id"]) == _local_client_id
		var player_color := Color("5bc0eb") if is_local else Color("8bd450")
		draw_circle(position, 12.0, player_color)
		draw_circle(position, 5.0, Color("102026"))
