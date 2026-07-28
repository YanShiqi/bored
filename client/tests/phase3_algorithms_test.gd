extends SceneTree

const SnapshotBufferType = preload("res://scripts/snapshot_buffer.gd")
const LocalPredictionType = preload("res://scripts/local_prediction.gd")

var _failure_count := 0


func _initialize() -> void:
	_test_snapshot_interpolation()
	_test_snapshot_ordering_and_hold()
	_test_prediction_and_reconciliation()

	if _failure_count == 0:
		print("phase3_algorithms_test: all tests passed")
		quit(0)
		return

	printerr("phase3_algorithms_test: %d assertion(s) failed" % _failure_count)
	quit(1)


func _test_snapshot_interpolation() -> void:
	var buffer = SnapshotBufferType.new()
	_expect(buffer.push_snapshot(10, 1_000_000, Vector2(0.0, 0.0)), "first snapshot is accepted")
	_expect(buffer.push_snapshot(11, 1_100_000, Vector2(100.0, 200.0)), "newer snapshot is accepted")

	var midpoint: Vector2 = buffer.sample_position(1_050_000)
	_expect(midpoint.is_equal_approx(Vector2(50.0, 100.0)), "position is interpolated at midpoint")


func _test_snapshot_ordering_and_hold() -> void:
	var buffer = SnapshotBufferType.new()
	buffer.push_snapshot(20, 2_000_000, Vector2(200.0, 0.0))
	_expect(not buffer.push_snapshot(20, 2_010_000, Vector2(999.0, 0.0)), "duplicate tick is rejected")
	_expect(not buffer.push_snapshot(19, 2_020_000, Vector2(999.0, 0.0)), "out-of-order tick is rejected")
	_expect(
		buffer.sample_position(1_900_000).is_equal_approx(Vector2(200.0, 0.0)),
		"time before the buffer holds the earliest snapshot"
	)
	_expect(
		buffer.sample_position(2_100_000).is_equal_approx(Vector2(200.0, 0.0)),
		"time after the buffer holds the latest snapshot without extrapolation"
	)


func _test_prediction_and_reconciliation() -> void:
	var prediction = LocalPredictionType.new()
	_expect(prediction.record_sent_input(1, 1, 1, 0), "first input is predicted")
	prediction.record_sent_input(2, 2, 1, 0)
	prediction.record_sent_input(3, 3, 1, 0)
	_expect(
		prediction.get_predicted_position_mm() == Vector2i(300, 0),
		"three inputs advance the predicted position"
	)

	# 服务端确认序号 1 后，客户端从权威 100 mm 起点重放序号 2、3，结果仍应为 300 mm。
	_expect(prediction.reconcile(100, Vector2i(100, 0), 1), "first authority snapshot is accepted")
	_expect(prediction.get_pending_input_count() == 2, "acknowledged input is removed")
	_expect(
		prediction.get_predicted_position_mm() == Vector2i(300, 0),
		"unacknowledged inputs are replayed in order"
	)

	# 下一份权威状态制造 150 mm 偏差，验证客户端最终采用“权威位置 + 剩余输入”。
	_expect(prediction.reconcile(101, Vector2i(50, 0), 2), "newer authority snapshot is accepted")
	_expect(prediction.get_predicted_position_mm() == Vector2i(150, 0), "authority correction changes prediction")
	_expect(is_equal_approx(prediction.get_last_prediction_error_mm(), 150.0), "correction error is measured")
	_expect(prediction.get_correction_count() == 1, "visible correction is counted")
	_expect(not prediction.reconcile(100, Vector2i.ZERO, 3), "stale authority snapshot is rejected")


func _expect(condition: bool, message: String) -> void:
	if condition:
		return
	_failure_count += 1
	printerr("FAILED: %s" % message)
