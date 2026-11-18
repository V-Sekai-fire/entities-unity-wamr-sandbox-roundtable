extends Node
func _say(s): print("BTCHECK ", s)
func _ready():
	await get_tree().process_frame
	for m in ["has_feature_jit","is_jit_enabled","has_feature_binary_translation"]:
		_say("%s=%s" % [m, str(ClassDB.class_call_static("Sandbox", m))])
	_say("setting enabled=%s dir=%s" % [str(ProjectSettings.get_setting("sandbox/binary_translation/enabled")), str(ProjectSettings.get_setting("sandbox/binary_translation/cache_dir"))])
	var sb = ClassDB.instantiate("Sandbox")
	sb.memory_max = 256
	sb.execution_timeout = int(OS.get_environment("GD_TIMEOUT") if OS.get_environment("GD_TIMEOUT") != "" else "100000")
	sb.program = load("res://bench_guest.elf")
	add_child(sb)
	_say("translated=%s is_jit=%s hash=%s" % [str(sb.is_binary_translated()), str(sb.is_jit()), str(sb.get_translation_hash())])
	var t0 := Time.get_ticks_usec()
	for i in 1000000:
		sb.vmcall("empty")
	_say("case1 empty x1000000 ms=%.1f (loop in GDScript)" % ((Time.get_ticks_usec() - t0) / 1000.0))
	t0 = Time.get_ticks_usec()
	var r = sb.vmcall("lbfgs", 50000, 20)
	_say("case4 lbfgs 1M iterations ms=%.1f result=%s" % [(Time.get_ticks_usec() - t0) / 1000.0, str(r)])
	get_tree().quit()
