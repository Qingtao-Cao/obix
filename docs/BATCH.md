# Batch Facility

## Overview

The batch mechanism is implemented to combine multiple requests into one to yield better network performance, which is basically an aggregation of normal read, write and invoke requests in a batchIn contract. The batch handler on the oBIX server returns a batchOut contract with a list of responses to each respective request, specifically, the number of responses must equal to that of the requests and the sequence of each response in the batchOut contract must equal to that of respective request in the batchIn contract.

All read and write requests are supported via a batch request, however, not all invoke requests are supported. Please see the next section for details.

Furthermore, considering that each sub batch command in a batch request are handled in a sequential order, the more number of commands aggregated the longer time it takes the oBIX server to handle it and therefore the longer time relevant client needs to wait for the overall batchOut response. Keep this in mind, oBIX clients had better batch only "small" requests which can be handled fairly quickly and raise time-consuming requests explicitly.


## Limitations On POST Handlers

The support of POST handlers via the batch mechanism are summarised below:

POST HANDLER | SUPPORTED?
------------ | ----------
handlerSignUp | Yes
handlerWatchServiceMake | Yes
handlerWatchDelete | Yes				
handlerWatchAdd | Yes
handlerWatchRemove | Yes
handlerWatchPollRefresh | Yes
handlerWatchPollChanges	| No
handlerHistoryGet | No
handlerHistoryQuery	| No
handlerHistoryAppend | No
handlerBatch | No



In the first place, a batch request should not be allowed to nest within another otherwise a malicious batch request with thousands of level of sub batch requests nested altogether can easily consume the stack memory of relevant oBIX server thread handling such request.

In the second place, all history handlers take care of sending back responses by themselves which are likely too massive to be sent via a batchOut contract. As a matter of fact, responses of history handlers are designed not to be sent back via obix_sever_reply_object(). Moreover, history data are sent independently from the batchOut contract, infringing relevant requirement in the oBIX specification.

In the third place, the polling threads handling watch.pollChanges requests will compete against the oBIX server thread handling the current batch request on sending through the same FCGI request different response (the watchOut and the batchOut contract to be exact) independently and worse still, then race to release the same FCGI request which ultimately is likely to bring about a segfault.
