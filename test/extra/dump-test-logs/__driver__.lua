check({"sh", initial_dir.."/extra/building/dump-test-logs.sh"},
      1, true, false)
check(qgrep("^### test/work/tester.log ###$", "stdout")
      and qgrep("^### test/work/unit.log ###$", "stdout")
      and qgrep("^### test/work/func.log ###$", "stdout")
      and qgrep("^### test/work/extra.log ###$", "stdout"))
