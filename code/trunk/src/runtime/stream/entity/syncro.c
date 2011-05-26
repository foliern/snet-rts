/** <!--********************************************************************-->
 *
 * $Id: syncro.c 2887 2010-10-17 19:37:56Z dlp $
 *
 * file syncro.c
 *
 * This implements the synchrocell component [...] TODO: write this...
 *
 * Flow inheritance at a synchrocell is (currently) defined in such a way
 * that only the record that matches the first pattern flow-inherits all
 * its constituents. The order of arrival is irrelevant. Previously, the
 * record that arrived last, i.e. the record that matched the last remaining
 * unmatched record, was to keep all its fields and tags.
 *
 * The necessary changes to implement the 'new' behaviour is minimal, as
 * construction of the resulting outbound record is implemented in a separate
 * merge function (see above ;)).
 * Where previously the last record was passed as argument to the merge
 * function, now the record which is stored at first position in the record
 * storage is passed to the merge function. As excess fields and tags, i.e.
 * all those labels that are not present in the synchro pattern, are not
 * stripped from records before they go into storage, the above is sufficient
 * to implement the desired behaviour.
 *
 *****************************************************************************/

#include <assert.h>

#include "snetentities.h"
#include "bool.h"
#include "memfun.h"
#include "threading.h"
#include "distribution.h"

/* ------------------------------------------------------------------------- */
/*  SNetSync                                                                 */
/* ------------------------------------------------------------------------- */

// Destroys storage including all records
static snet_record_t *merge( snet_record_t **storage,
                             snet_variant_list_t *patterns)
{
  int i, name;
  snet_variant_t *pattern;
  snet_record_t *result = storage[0];

  LIST_ENUMERATE(patterns, pattern, i)
    if (i > 0) {
      VARIANT_FOR_EACH_FIELD(pattern, name)
        if (!SNetRecHasField( result, name)) {
          SNetRecSetField(result, name, SNetRecTakeField(storage[i], name));
        }
      END_FOR

      VARIANT_FOR_EACH_TAG(pattern, name)
        if (!SNetRecHasTag( result, name)) {
          SNetRecSetTag( result, name, SNetRecGetTag( storage[i], name));
        }
      END_FOR
    }
  END_ENUMERATE

  for (i = 1; i < SNetVariantListLength( patterns); i++) {
    SNetRecDestroy( storage[i]);
  }

  return result;
}

typedef struct {
  snet_stream_t *input, *output;
  snet_variant_list_t *patterns;
  snet_expr_list_t *guard_exprs;
} sync_arg_t;

/**
 * Sync box task
 */
static void SyncBoxTask(void *arg)
{
  int i;
  int match_cnt=0, new_matches=0;
  int num_patterns;
  bool terminate = false;
  sync_arg_t *sarg = (sync_arg_t *) arg;
  snet_stream_desc_t *outstream, *instream;
  snet_record_t **storage;
  snet_record_t *rec;
  snet_variant_t *pattern;
  snet_locvec_t *instream_source = NULL;

  instream  = SNetStreamOpen(sarg->input,  'r');
  outstream = SNetStreamOpen(sarg->output, 'w');

  num_patterns = SNetVariantListLength( sarg->patterns);
  storage = SNetMemAlloc(num_patterns * sizeof(snet_record_t*));
  for(i = 0; i < num_patterns; i++) {
    storage[i] = NULL;
  }
  match_cnt = 0;
  //FIXME: Clean up more

  /* MAIN LOOP START */
  while( !terminate) {
    /* read from input stream */
    rec = SNetStreamRead( instream);

    switch (SNetRecGetDescriptor( rec)) {
      case REC_data:
        new_matches = 0;
        LIST_ENUMERATE(sarg->patterns, pattern, i)
          /* storage empty and guard accepts => store record*/
          if ((storage[i] == NULL) &&
              (SNetRecPatternMatches(pattern, rec)) &&
              (
               i >= SNetExprListLength(sarg->guard_exprs) ||
               SNetEevaluateBool(SNetExprListGet(sarg->guard_exprs, i), rec)
              )) {
            storage[i] = rec;
            new_matches += 1;
          }
        END_ENUMERATE

        if (new_matches == 0) {
          SNetStreamWrite( outstream, rec);
        } else {
          match_cnt += new_matches;
          if(match_cnt == num_patterns) {
            SNetStreamWrite( outstream, merge( storage, sarg->patterns));

            if (instream_source != NULL) {
              /* predecessor made known its location,
               * presumably for garbage collection, so we will forward it
               * to let our successor know
               */
              SNetStreamWrite( outstream,
                  SNetRecCreate(REC_source, instream_source));
            }
            /* follow by a sync record */
            SNetStreamWrite( outstream, SNetRecCreate(REC_sync, sarg->input));

            /* the receiver of REC_sync will destroy the outstream */
            SNetStreamClose( outstream, false);
            /* instream has been sent to next entity, do not destroy  */
            SNetStreamClose( instream, false);


            terminate = true;
          }
        }
        break;

      case REC_sync:
        {
          sarg->input = SNetRecGetStream( rec);
          SNetStreamReplace( instream, sarg->input);
          SNetRecDestroy( rec);
        }
        break;

      case REC_sort_end:
        /* forward sort record */
        SNetStreamWrite( outstream, rec);
        break;

      case REC_terminate:
        terminate = true;
        SNetStreamWrite( outstream, rec);
        SNetStreamClose( outstream, false);
        SNetStreamClose( instream, true);
        break;

      case REC_source:
        /* Get (a copy of) the location */
        if (instream_source != NULL) {
          SNetLocvecDestroy(instream_source);
        }
        instream_source = SNetLocvecCopy( SNetRecGetLocvec( rec));
        SNetRecDestroy( rec);
        break;

      case REC_collect:
        /* invalid record */
      default:
        assert(0);
        /* if ignore, destroy at least ... */
        SNetRecDestroy( rec);
    }
  } /* MAIN LOOP END */

  /* free any stored location vector */
  if (instream_source != NULL) {
    SNetLocvecDestroy(instream_source);
  }

  SNetMemFree(storage);

  SNetVariantListDestroy( sarg->patterns);
  SNetExprListDestroy( sarg->guard_exprs);
  SNetMemFree( sarg);
}



/**
 * Synchro-Box creation function
 */
snet_stream_t *SNetSync( snet_stream_t *input,
    snet_info_t *info,
    int location,
    snet_variant_list_t *patterns,
    snet_expr_list_t *guard_exprs )
{
  snet_stream_t *output;
  sync_arg_t *sarg;

  input = SNetRouteUpdate(info, input, location);
  if(location == SNetNodeLocation) {
    output = SNetStreamCreate(0);
    sarg = (sync_arg_t *) SNetMemAlloc( sizeof( sync_arg_t));
    sarg->input  = input;
    sarg->output = output;
    sarg->patterns = patterns;
    sarg->guard_exprs = guard_exprs;

    SNetEntitySpawn( ENTITY_SYNC, SyncBoxTask, (void*)sarg);
  } else {
    SNetVariantListDestroy( patterns);
    SNetExprListDestroy( guard_exprs);
    output = input;
  }

  return output;
}