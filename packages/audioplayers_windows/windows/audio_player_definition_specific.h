public:
	// Win
	GMainLoop* _g_main_loop = nullptr;
	std::thread _thread;

	void thread_start();
	void thread_end();
