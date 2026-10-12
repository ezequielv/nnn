#include <Directory.h>
#include <Looper.h>
#include <NodeMonitor.h>
#include <MessageFilter.h>

#include "haiku_interop.h"

filter_result dir_mon_flt(BMessage *message, BHandler **hnd, BMessageFilter *fltr) {
	(void) hnd;
	(void) fltr;

	if (message->what == B_NODE_MONITOR) {
		int32 val;
		message->FindInt32("opcode", &val);

		switch (val) {
			case B_ENTRY_CREATED:
			case B_ENTRY_MOVED:
			case B_ENTRY_REMOVED:
				return B_DISPATCH_MESSAGE;
		}
	}

	return B_SKIP_MESSAGE;
}

class DirectoryListener : public BLooper {
public:
	bool recv_reset() {
		Lock();
		bool val = _ev_on;
		_ev_on = false;
		Unlock();

		return val;
	}
private:
	void MessageReceived(BMessage * message) override {
		Lock();
		_ev_on = true;
		Unlock();
		BLooper::MessageReceived(message);
	}

	bool _ev_on = false;
};

struct haiku_nm_t {
	haiku_nm_t() {
		dl = new DirectoryListener();
		flt = new BMessageFilter(B_PROGRAMMED_DELIVERY, B_LOCAL_SOURCE, dir_mon_flt);
		dl->AddCommonFilter(flt);
		dl->Run();
	}

#if ISSUE_HAIKU_DOUBLE_FREE_ON_EXIT_MODE
	~haiku_nm_t() {
#if ISSUE_HAIKU_DOUBLE_FREE_ON_EXIT_MODE == 1
		// do nothing with 'flt':
		//  * the crash should at least happen less often than before,
		//    as "no free" is "better" than "double free" in this
		//    regard;
		//  * and either/both of these happen:
		//   * 'BLooper::PostMessage(B_QUIT_REQUESTED)' causes the
		//      linked/attached 'BMessageFilter' to be ultimately
		//      deleted for us;
		//   * there is no issue with leaked memory because currently
		//     this is called just before 'main()' returns, which
		//     should let the os clean up resources for this process,
		//     including memory;
#elif ( ISSUE_HAIKU_DOUBLE_FREE_ON_EXIT_MODE == 2 ) || ( ISSUE_HAIKU_DOUBLE_FREE_ON_EXIT_MODE == 3 )
#if ISSUE_HAIKU_DOUBLE_FREE_ON_EXIT_MODE == 2
		// NOTE: the 'RemoveCommonFilter()' does not seem to work. debugger reports: "looper must be locked before proceeding"
#elif ISSUE_HAIKU_DOUBLE_FREE_ON_EXIT_MODE == 3
		// NOTE: 'Lock()'-ing and 'Unlock()'-ing around the 'RemoveCommonFilter()' seems to work so far.
		dl->Lock(); // ignore retval
#endif // ISSUE_HAIKU_DOUBLE_FREE_ON_EXIT_MODE
		dl->RemoveCommonFilter(flt); // ignore retval
		delete flt;
#if ISSUE_HAIKU_DOUBLE_FREE_ON_EXIT_MODE == 3
		dl->Unlock(); // returns void
#endif // ISSUE_HAIKU_DOUBLE_FREE_ON_EXIT_MODE
#else // ISSUE_HAIKU_DOUBLE_FREE_ON_EXIT_MODE
#	error unsupported value for ISSUE_HAIKU_DOUBLE_FREE_ON_EXIT_MODE (supported values: undefined/0 (disabled), 1..3)
#endif // ISSUE_HAIKU_DOUBLE_FREE_ON_EXIT_MODE
		// This is the way of deleting a BLooper
		dl->PostMessage(B_QUIT_REQUESTED);
	}
#endif // ISSUE_HAIKU_DOUBLE_FREE_ON_EXIT_MODE

	DirectoryListener *dl;
	BMessageFilter *flt;
	node_ref nr;
};

haiku_nm_h haiku_init_nm() {
	return new haiku_nm_t();
}

void haiku_close_nm(haiku_nm_h hnd) {
#if ! ISSUE_HAIKU_DOUBLE_FREE_ON_EXIT_MODE
	delete hnd->flt;
	// This is the way of deleting a BLooper
	hnd->dl->PostMessage(B_QUIT_REQUESTED);
#endif // ISSUE_HAIKU_DOUBLE_FREE_ON_EXIT_MODE
	delete hnd;
}
int haiku_watch_dir(haiku_nm_h hnd, const char *path) {
	BDirectory dir(path);
	dir.GetNodeRef(&(hnd->nr));

	return watch_node(&(hnd->nr), B_WATCH_DIRECTORY, nullptr, hnd->dl);
}
int haiku_stop_watch(haiku_nm_h hnd) {
	return watch_node(&(hnd->nr), B_STOP_WATCHING, nullptr, hnd->dl);
}

int haiku_is_update_needed(haiku_nm_h hnd) {
	return hnd->dl->recv_reset();
}
