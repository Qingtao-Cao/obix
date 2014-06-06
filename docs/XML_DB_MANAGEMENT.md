# XML Database Management

Above all, the oBIX Server is a huge XML database (or XML DOM tree) that hosts a great number of XML objects (or oBIX contracts) signed up or appended by oBIX adapters such as devices and their history records.

At the time of this writing, the xmldb_update_node() in xml_storage.c can alter existing objects in the XML database in the following manner:

* Change the "val" attribute of the node of the given href
* Insert any children node from the provided input document to the destination node in the XML database
* Remove any matching reference node specified in the input document from the destination node in the XML database.

Reference nodes can be created and removed (thus updated/replaced) on the fly to setup the dynamic connection between multiple oBIX contracts without introducing further inconsistency issues in the XML database when a normal object is deleted/replaced, since watch items may have been installed in there, relevant descriptors must be removed from relevant watch objects and poll tasks on the changed node or any of its ancestors must be notified.

The addChildren and removeRef scripts in tests/scripts/ can be used to illustrate how the insertion and deletion of the reference nodes are supported. The signUp test script shall be run first to register the required example device.

