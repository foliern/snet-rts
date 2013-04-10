#ifndef _GLUE_SNET_H
#define _GLUE_SNET_H

#define SNET_SOURCE_PREFIX 		"snet_source"
#define SNET_SINK_PREFIX 			"snet_sink"

#include "threading.h"
#include "string.h"
size_t GetStacksize(snet_entity_descr_t descr);
void *EntityTask(void *arg);


#endif  /* _GLUE_SNET_H */

