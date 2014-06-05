# Watch Subsystem

A super oBIX watch subsystem has been implemented to provide fantastic scalability, flexibility and performance. So far, it supports the following major features:

* No limitation on the number of watches.
* No limitation on the number of objects monitored by one watch.
* No limitation on the number of oBIX clients sharing one watch.
* Multiple watches able to monitor one same object, particuarly nested watches installed at different levels in one subtree.
* Long poll mechanism.
* Support parallelism and thread safe, specifically, multiple long poll threads handling poll tasks simultaneously to yield minimal latency.
* Recyclable watch IDs, no worries about watch ID counter's overflow (by manipulating extensible bitmap nodes).

Watch relevant scripts in tests/scripts/ can be used to test the watch subsystem.

A single watch object can be created by watchMakeSingle script and watchAddSingle script can be called for it several times to have multiple objects added to its watched upon list. The watchMake script can also be used to create a number of watches in a batch mode.

It's worthwhile to note that one watch object should avoid monitoring different objects that are ancestors or descendants of each other. That is, objects at a different level in one subtree, which is not only redundant but may result in
some unexpected side effects such as one watchOut contract may not contain all positive changes in the response to one Watch.pollChange request.

This occurs because long poll threads are working asynchronously with the thread that notifies all watches installed on ancestor objects one after the other, therefore one long poll thread could send out watchOut contract *before* all
watch_item_t for descendant objects in one watch can be properly notified. However, fortunately, this is not a fatal race condition since no changes would ever be lost and an extra pollChange request on the same watch object will collect any remaining changes.

Again, different watch objects are free to monitor whatever objects, but one watch object should avoid keeping an eye on both parent and children object.

After a watch object is fully loaded, watchPollChange script can be used to have it waiting for any changes on any monitored object, while the watchPollRefresh script is useful not only to reset any existing changes, but also to show the full list of all objects monitored by one specified watch object.

The watchRemoveSingle script could be used to remove one specified object from the watched upon list of the given watch. The watchDelete script could be used to remove a watch object completely.

It's also worthwhile to note that if a watch object is currently waiting for a change, the use of watchDelete will interrupt its waiting and have it returned prematurely so as to be deleted properly.

Due to the usage of extensible bitmap, any IDs of deleted watch objects can be properly recycled, therefore eliminating the potential overflow of a plain watch ID counter.

Lastly, the watchDeleteSingle script can delete the specified watch object, whereas the watchDeleteAll script will delete all watch objects created on the oBIX Server. They are especillay useful to test the recycling of watch IDs.

