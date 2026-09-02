def opx_run_client_server_test(cmdline_args, executable, iteration_type,
                               completion_semantic, memory_type,
                               warmup_iteration_type=None,
                               completion_type="queue"):
    from common import ClientServerTest

    # datacheck_type="with_datacheck" byte-verifies the payload device->host,
    # which is the whole point of exercising the opx GPU (FI_HMEM) path.
    test = ClientServerTest(cmdline_args, executable, iteration_type,
                            completion_semantic=completion_semantic,
                            datacheck_type="with_datacheck",
                            memory_type=memory_type,
                            warmup_iteration_type=warmup_iteration_type,
                            completion_type=completion_type)
    test.run()
