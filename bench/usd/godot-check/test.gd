extends Node
# Real-window check of binary translation in Godot's own sandbox, running usd.elf on a .usdz.
# Env: USDZ = path to the package. Prints lines starting "BTCHECK ".

func _say(s: String) -> void:
	print("BTCHECK ", s)

func _ready() -> void:
	await get_tree().process_frame
	var usdz := OS.get_environment("USDZ")
	_say("godot %s renderer=%s" % [Engine.get_version_info().string, RenderingServer.get_video_adapter_name()])
	_say("has_feature_binary_translation=%s" % str(ClassDB.class_call_static("Sandbox", "has_feature_binary_translation") if ClassDB.class_has_method("Sandbox", "has_feature_binary_translation") else "n/a"))
	var t0 := Time.get_ticks_usec()
	var sb = ClassDB.instantiate("Sandbox")
	sb.memory_max = 1024
	sb.allocations_max = 10000000
	sb.references_max = 4096
	sb.execution_timeout = 100000
	sb.program = load("res://usd.elf")
	add_child(sb)
	_say("create_ms=%.1f is_binary_translated=%s" % [(Time.get_ticks_usec() - t0) / 1000.0, str(sb.is_binary_translated())])
	t0 = Time.get_ticks_usec()
	var r1 = sb.vmcall("usd_init")
	_say("usd_init_ms=%.1f -> %s is_binary_translated=%s" % [(Time.get_ticks_usec() - t0) / 1000.0, str(r1).left(60), str(sb.is_binary_translated())])
	var bytes := FileAccess.get_file_as_bytes(usdz)
	_say("package=%s bytes=%d" % [usdz.get_file(), bytes.size()])
	t0 = Time.get_ticks_usec()
	var piece := 8 << 20
	var off := 0
	while off < bytes.size():
		var n: int = min(piece, bytes.size() - off)
		sb.vmcall("usd_push", bytes.slice(off, off + n))
		off += n
	var t_push := (Time.get_ticks_usec() - t0) / 1000.0
	t0 = Time.get_ticks_usec()
	var r2 = sb.vmcall("usd_open_staged")
	_say("push_ms=%.1f open_ms=%.1f -> %s" % [t_push, (Time.get_ticks_usec() - t0) / 1000.0, str(r2).left(120)])
	_say("is_binary_translated_after=%s" % str(sb.is_binary_translated()))
	get_tree().quit()
