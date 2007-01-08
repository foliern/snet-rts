/*
 * This implements S-Net entities.
 *
 * NOTE: all created threads are detached.
 */


#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdarg.h>
#include <unistd.h>
#include <semaphore.h>

#include <memfun.h>
#include <record.h>

#include <bool.h>


#include <snetentities.h>



/* --------------------------------------------------------
 * Syncro Cell: Flow Inheritance, uncomment desired variant
 * --------------------------------------------------------
 */
 
// inherit all fields and tags from matched records
//#define SYNC_FI_VARIANT_1

// inherit no tags/fields from previous matched records
// merge tags/fields from pattern to last arriving record.
#define SYNC_FI_VARIANT_2
/* --------------------------------------------------------
 */


#ifdef DEBUG
  #include <debug.h>
  extern debug_t *GLOBAL_OUTBUF_DBG;
  #define BUF_CREATE( PTR, SIZE)\
            PTR = SNetBufCreate( SIZE);\
            DBGregisterBuffer( GLOBAL_OUTBUF_DBG, PTR);

  #define BUF_SET_NAME( PTR, NAME)\
            DBGsetName( GLOBAL_OUTBUF_DBG, PTR, NAME);
#else
  #define BUF_CREATE( PTR, SIZE)\
            PTR = SNetBufCreate( SIZE);

  #define BUF_SET_NAME( PTR, NAME)
#endif

int warn_parallel = 0, warn_star = 0;


typedef struct {
  bool is_match;
  int match_count;
} match_count_t;




typedef struct {
  snet_buffer_t *from_buf;
  snet_buffer_t *to_buf;
  char *desc;
} dispatch_info_t;


typedef struct {
  snet_buffer_t *buf;
  int num;
} snet_blist_elem_t;

// used in SNetOut (depends on local variables!)
#define ENTRYSUM( RECNUM, TENCNUM)\
                        RECNUM( HNDgetRecord( hnd)) +\
                        TENCNUM( TENCgetVariant( HNDgetType( hnd), variant_num))

// used in SNetOut (depends on local variables!)
#define FILL_LOOP( RECNUM, RECNAME, TENCNUM, SET1, SET2 )\
  i = 0; cnt = 0;\
  while( cnt < RECNUM( HNDgetRecord( hnd))) {\
    int name;\
    name = RECNAME( HNDgetRecord( hnd))[i];\
    if( name != CONSUMED ) {\
      SET1 ;\
      cnt = cnt + 1;\
    }\
    i = i + 1;\
  }\
  for ( i=0; i < TENCNUM( TENCgetVariant( HNDgetType( hnd), variant_num)); i++) {\
    SET2 ;\
    cnt = cnt + 1;\
  }\

// used in SNetOut (depends on local variables!)
#define FILLVECTOR( RECNUM, RECNAME, NAMES, TENCNUM, TENCNAME) \
          FILL_LOOP( RECNUM, RECNAME, TENCNUM, \
            TENCsetVectorEntry( NAMES, cnt, RECNAME( HNDgetRecord( hnd))[i]),\
            TENCsetVectorEntry( NAMES, cnt, TENCNAME( TENCgetVariant( HNDgetType( hnd), variant_num))[i]))

// used in SNetOut (depends on local variables!)
#define FILLRECORD( RECNUM, RECNAME, RECSET, RECGET, TENCNUM, TENCNAME, TYPE) \
          FILL_LOOP( RECNUM, RECNAME, TENCNUM, \
            RECSET( outrec, name, RECGET( HNDgetRecord( hnd), name)),\
            RECSET( outrec, TENCNAME( TENCgetVariant( HNDgetType( hnd), variant_num))[i], va_arg( args, TYPE)))


// used in SNetParallelDet->CheckMatch (depends on local variables!)
#define FIND_NAME_LOOP( RECNUM, TENCNUM, RECNAMES, TENCNAMES)\
for( i=0; i<TENCNUM( venc); i++) {\
       if( !( ContainsName( TENCNAMES( venc)[i], RECNAMES( rec), RECNUM( rec)))) {\
        mc->is_match = false;\
      }\
      else {\
        mc->match_count += 1;\
      }\
    }



// used in SNetSync->IsMatch (depends on local variables!)
#define FIND_NAME_IN_PATTERN( TENCNUM, RECNUM, TENCNAMES, RECNAMES)\
  names = RECNAMES( rec);\
  for( i=0; i<TENCNUM( pat); i++) {\
    for( j=0; j<RECNUM( rec); j++) {\
      if( names[j] == TENCNAMES( pat)[i]) {\
        found_name = true;\
        break;\
      }\
    }\
    if ( !( found_name)) {\
      is_match = false;\
      break;\
    }\
    found_name = false;\
  }\
  SNetMemFree( names);


#define FILL_NAME_VECTOR( RECNAMES, RECNUM, TENCNAMES, TENCNUM, VECT_NAME)\
  last_entry = -1;\
  names = RECNAMES( rec);\
  for( i=0; i<RECNUM( rec); i++) {\
    TENCsetVectorEntry( VECT_NAME, i, names[i]);\
    last_entry = i;\
  }\
  SNetMemFree( names);\
  names = TENCNAMES( venc);\
  for( i=0; i<TENCNUM( venc); i++) {\
    TENCsetVectorEntry( VECT_NAME, ++last_entry, names[i]);\
  }


/* ------------------------------------------------------------------------- */
/*  SNetOut                                                                  */
/* ------------------------------------------------------------------------- */


extern snet_handle_t *SNetOut( snet_handle_t *hnd, int variant_num, ...) {

  int i, *names;
  va_list args;
  snet_record_t *out_rec, *old_rec;
  snet_variantencoding_t *venc;


  // set values from box

  if( variant_num > 0) { /* if variant_num == 0 => box produced nothing (empty to empty) */

   venc = SNetTencGetVariant( SNetHndGetType( hnd), variant_num);
   out_rec = SNetRecCreate( REC_data, SNetTencCopyVariantEncoding( venc));

   va_start( args, variant_num);
   names = SNetTencGetFieldNames( venc);
   for( i=0; i<SNetTencGetNumFields( venc); i++) {
     SNetRecSetField( out_rec, names[i], va_arg( args, void*));
   }
   names = SNetTencGetTagNames( venc);
   for( i=0; i<SNetTencGetNumTags( venc); i++) {
     SNetRecSetTag( out_rec, names[i], va_arg( args, int));
   }
   names = SNetTencGetBTagNames( venc);
   for( i=0; i<SNetTencGetNumBTags( venc); i++) {
     SNetRecSetBTag( out_rec, names[i], va_arg( args, int));
   }
   va_end( args);
  }
  else {
    venc = SNetTencVariantEncode( SNetTencCreateVector( 0),
                                  SNetTencCreateVector( 0),
                                  SNetTencCreateVector( 0));
    out_rec = SNetRecCreate( REC_data, venc);
  }


  // flow inherit

  old_rec = SNetHndGetRecord( hnd);

  names = SNetRecGetUnconsumedFieldNames( old_rec);
  for( i=0; i<SNetRecGetNumFields( old_rec); i++) {
    if( SNetRecAddField( out_rec, names[i])) {
      SNetRecSetField( out_rec, names[i], SNetRecGetField( old_rec, names[i]));
    }
  }
  SNetMemFree( names);

  names = SNetRecGetUnconsumedTagNames( old_rec);
  for( i=0; i<SNetRecGetNumTags( old_rec); i++) {
    if( SNetRecAddTag( out_rec, names[i])) {
      SNetRecSetTag( out_rec, names[i], SNetRecGetTag( old_rec, names[i]));
    }
  }
  SNetMemFree( names);


  // output record
  SNetBufPut( SNetHndGetOutbuffer( hnd), out_rec);

  return( hnd);
}


/* ------------------------------------------------------------------------- */
/*  SNetBox                                                                  */
/* ------------------------------------------------------------------------- */

static void *BoxThread( void *hndl) {

  snet_handle_t *hnd;
  snet_record_t *rec;
  void (*boxfun)( snet_handle_t*);
  bool terminate = false;
  hnd = (snet_handle_t*) hndl;
  boxfun = SNetHndGetBoxfun( hnd);

  while( !( terminate)) {
    rec = SNetBufGet( SNetHndGetInbuffer( hnd));

    switch( SNetRecGetDescriptor( rec)) {
      case REC_data:
        SNetHndSetRecord( hnd, rec);
        (*boxfun)( hnd);
        SNetRecDestroy( rec);
        break;
      case REC_sync:
        SNetHndSetInbuffer( hnd, SNetRecGetBuffer( rec));
        SNetRecDestroy( rec);
        break;
      case REC_collect:
        printf("\nUnhandled control record, destroying it.\n\n");
        SNetRecDestroy( rec);
        break;
      case REC_sort_begin:
        SNetBufPut( SNetHndGetOutbuffer( hnd), rec);
        break;
      case REC_sort_end:
        SNetBufPut( SNetHndGetOutbuffer( hnd), rec);
        break;        
      case REC_terminate: 
        terminate = true;
        SNetBufPut( SNetHndGetOutbuffer( hnd), rec);
        SNetBufBlockUntilEmpty( SNetHndGetOutbuffer( hnd));
        SNetBufDestroy( SNetHndGetOutbuffer( hnd));
        SNetHndDestroy( hnd);
        break;
    }
  }
   return( NULL);
 
}

extern snet_buffer_t *SNetBox( snet_buffer_t *inbuf, void (*boxfun)( snet_handle_t*), 
                                   snet_typeencoding_t *outspec) {

  snet_buffer_t *outbuf;
  snet_handle_t *hndl;
  pthread_t *box_thread;
  

//  outbuf = SNetBufCreate( BUFFER_SIZE);
  BUF_CREATE( outbuf, BUFFER_SIZE);
  hndl = SNetHndCreate( HND_box, inbuf, outbuf, NULL, boxfun, outspec);

  box_thread = SNetMemAlloc( sizeof( pthread_t));
  pthread_create( box_thread, NULL, (void*)BoxThread, (void*)hndl );
  
  pthread_detach( *box_thread);

  return( outbuf);
}


/* ------------------------------------------------------------------------- */
/*  SNetSerial                                                               */
/* ------------------------------------------------------------------------- */

extern snet_buffer_t *SNetSerial( snet_buffer_t *inbuf, snet_buffer_t* (*box_a)( snet_buffer_t*), 
                                              snet_buffer_t* (*box_b)( snet_buffer_t*)) {

  snet_buffer_t *internal_buf;
  snet_buffer_t *outbuf;

  internal_buf = (*box_a)( inbuf);
  outbuf = (*box_b)( internal_buf);

  return( outbuf);
}

/* ========================================================================= */
/* double linked list ------------------------------------------------------ */

typedef struct {
  void *elem;
  void *prev; 
  void *next;
} list_elem_t;

typedef struct {
  list_elem_t *head;
  list_elem_t *curr;
  list_elem_t *last;
  int elem_count;
} linked_list_t;

static linked_list_t *LLSTcreate( void *first_item) {
  linked_list_t *lst;
  list_elem_t *lst_elem;

  lst_elem = malloc( sizeof( list_elem_t));
  lst_elem->elem = first_item;
  lst_elem->prev = NULL;
  lst_elem->next = NULL;

  lst = malloc( sizeof( linked_list_t));
  lst->elem_count = 1;

  lst->head = lst_elem;
  lst->curr = lst_elem;
  lst->last = lst_elem;
  lst->head->prev = NULL;
  lst->head->prev = NULL;

  return( lst);
}

static void *LLSTget( linked_list_t *lst) {
  if( lst->elem_count > 0) {
    return( lst->curr->elem);
  }
  else {
    printf("\nWarning, returning NULL (list empty)\n\n");
    return( NULL);
  }
}

static void LLSTset( linked_list_t *lst, void *elem) {
  if( lst->elem_count > 0) {
    lst->curr->elem = elem;
  }
}

static void LLSTnext( linked_list_t *lst) {
  if( lst->elem_count > 0) {
    lst->curr = ( lst->curr->next == NULL) ? lst->head : lst->curr->next;
  }
}

/*
static void LLSTprev( linked_list_t *lst) {
  if( lst->elem_count > 0) {
    lst->curr = (lst->curr->prev == NULL) ? lst->last : lst->curr->prev;
  }
}
*/


static void LLSTadd( linked_list_t *lst, void *elem) {
  list_elem_t *new_elem;

  new_elem = malloc( sizeof( list_elem_t));
  new_elem->elem = elem;
  new_elem->next = NULL;
  new_elem->prev = lst->last;
  
  if( lst->elem_count == 0) {
    lst->head = new_elem;
    lst->curr = new_elem;
  }
  else {
    lst->last->next = new_elem;
  }
  lst->last = new_elem;

  lst->elem_count += 1;
}


static void LLSTremoveCurrent( linked_list_t *lst) {

  list_elem_t *tmp;

  if( lst->elem_count == 0) {
    printf("\n\n ** Fatal Error ** : Can't delete from empty list.\n\n");
    exit( 1);
  }

  // just one element left
  if( lst->head == lst->last) {
    free( lst->curr);
    lst->head = NULL;
    lst->curr = NULL;
    lst->last = NULL;
  }
  else {
    // deleting head
    if( lst->curr == lst->head) {
      ((list_elem_t*)lst->curr->next)->prev = NULL;
      tmp = lst->curr;
      lst->head = lst->curr->next;
      lst->curr = lst->curr->next;
      free( tmp);
    } 
    else {
      // deleting last element
      if( lst->curr == lst->last) {
        ((list_elem_t*)lst->curr->prev)->next = NULL;
        tmp = lst->curr;
        lst->last = lst->curr->prev;
        lst->curr = lst->curr->prev;
        free( tmp);
      }
      // delete ordinary element
      else {
      ((list_elem_t*)lst->curr->next)->prev = lst->curr->prev;  
      ((list_elem_t*)lst->curr->prev)->next = lst->curr->next;
      tmp = lst->curr;
      lst->curr = lst->curr->prev;
      free( tmp);
      }
    } 
  }
  lst->elem_count -= 1;
} 

static int LLSTgetCount( linked_list_t *lst) {
  return( lst->elem_count);
}
/* ========================================================================= */





/* ============================================================= */
/* ============================================================= */

static void *DetCollector( void *info) {

  dispatch_info_t *inf = (dispatch_info_t*)info;
  snet_buffer_t *outbuf = inf->to_buf;
  snet_buffer_t *current_buf;

  sem_t *sem;
  snet_record_t *rec;
  linked_list_t *lst;

  bool terminate = false;
  bool got_record;

  int last_seen = -1;  
  int completed_buffers = 0;

  sem = SNetMemAlloc( sizeof( sem_t));
  sem_init( sem, 0, 0);

  SNetBufRegisterDispatcher( inf->from_buf, sem);
  
  lst = LLSTcreate( (void*)inf->from_buf);

  while( !( terminate)) {

    while( completed_buffers < LLSTgetCount( lst)) {
      got_record = false;  

      while( !( got_record)) {
        current_buf = (snet_buffer_t*)LLSTget( lst);

        sem_wait( sem);
        sem_post( sem); /* wait here if all buffers are empty, sem_post because */
        rec = SNetBufShow( current_buf); /* sem_wait is called again in switch() */
  
        if( rec != NULL) {
          got_record = true;
        
          if( SNetRecGetDescriptor( rec) == REC_sort_begin) {
            if( SNetRecGetNum( rec) == ( last_seen + 1)) {
            
            
              if( SNetRecGetLevel( rec) != 0) {
               printf("\n\nDBG :: level not 0\n\n"); 
              }
              bool done = false;
            
              while( !( done)) {
              
                rec = SNetBufGet( current_buf);
                sem_wait( sem);

			          switch( SNetRecGetDescriptor( rec)) {
			            case REC_data:
			              SNetBufPut( outbuf, rec); 
			              break;
			  
			            case REC_sync:
			              LLSTset( lst, SNetRecGetBuffer( rec));
			              SNetBufRegisterDispatcher( SNetRecGetBuffer( rec), sem);
			             SNetRecDestroy( rec);
			             break;
			  
			           case REC_collect:
			              LLSTadd( lst, SNetRecGetBuffer( rec));
			             SNetBufRegisterDispatcher( SNetRecGetBuffer( rec), sem);
			             SNetRecDestroy( rec);
			             break;
			
	  		          case REC_sort_begin:
                   if( SNetRecGetLevel( rec) != 0) {
                     SNetRecSetLevel( rec, SNetRecGetLevel( rec) - 1);
                     SNetBufPut( outbuf, rec);
                   }
                   else { 
        //            printf("\ncounter of start %d\n", SNetRecGetNum( rec));
                    //SNetRecDestroy( rec); 
                   }
		  	          break;
			          
			            case REC_sort_end:
                   if( SNetRecGetLevel( rec) == 0) {
                     //SNetRecDestroy( rec);
                     done = true; 
                     completed_buffers += 1;
      //               printf("\ncompleted buffers: %d/%d\n", completed_buffers, LLSTgetCount( lst));
                   }
                  else {
                     SNetRecSetLevel( rec, SNetRecGetLevel( rec) - 1);
                     SNetBufPut( outbuf, rec);
                  }
			           break;
			
			            case REC_terminate:
			             SNetBufDestroy( LLSTget( lst));
			             LLSTremoveCurrent( lst);
			             if( LLSTgetCount( lst) == 0) {
			               terminate = true;
			               SNetBufPut( outbuf, rec);
			               SNetBufBlockUntilEmpty( outbuf);
			               SNetBufDestroy( outbuf);
			             }
			             break;
			         } // switch
               
            } /* while */ 
         }
         else { /* last_seen does not match */
           //printf("last seen didn't match\n");
           LLSTnext( lst);

         }
        }
        else { /* shouldn't happen! (rec is not a sort_begin_token) */
            
          printf("happened[%d]\n", SNetRecGetDescriptor( rec));
        }
          
          
        }
        else { /* BufShow was NULL */
          LLSTnext( lst); 
        }
        
      }
    }
    completed_buffers = 0;
    last_seen += 1;
    //printf("last seen: %d\n", last_seen); 
 }
        

  SNetMemFree( inf);
  return( NULL);
}
/* ============================================================= */
/* ============================================================= */









/*
static void *Collector( void *info) {

  dispatch_info_t *inf = (dispatch_info_t*)info;
  snet_buffer_t *outbuf = inf->to_buf;
  snet_buffer_msg_t msg;
  sem_t *sem;
  snet_record_t *rec;
  linked_list_t *lst;
  bool terminate = false;
  bool got_record;


  sem = SNetMemAlloc( sizeof( sem_t));
  sem_init( sem, 0, 0);

  SNetBufRegisterDispatcher( inf->from_buf, sem);
  
  lst = LLSTcreate( (void*)inf->from_buf);

  while( !( terminate)) {
    got_record = false;
    sem_wait( sem);
    while( !( got_record)) {
      rec = SNetBufTryGet( (snet_buffer_t*)LLSTget( lst), &msg);
      if( msg == BUF_success) { 
       
        got_record = true;
        
        switch( SNetRecGetDescriptor( rec)) {
          case REC_data:
            SNetBufPut( outbuf, rec);
            break;
  
          case REC_sync:
            LLSTset( lst, SNetRecGetBuffer( rec));
            SNetBufRegisterDispatcher( SNetRecGetBuffer( rec), sem);
            SNetRecDestroy( rec);
            break;
  
          case REC_collect:
            LLSTadd( lst, SNetRecGetBuffer( rec));
            SNetBufRegisterDispatcher( SNetRecGetBuffer( rec), sem);
            SNetRecDestroy( rec);
            break;
          case REC_sort_begin:
            SNetBufPut( outbuf, rec);
            break;
          case REC_sort_end:
            SNetBufPut( outbuf, rec);
            break;
          case REC_terminate:
            SNetBufDestroy( LLSTget( lst));
            LLSTremoveCurrent( lst);
            if( LLSTgetCount( lst) == 0) {
              terminate = true;
              SNetBufPut( outbuf, rec);
              SNetBufBlockUntilEmpty( outbuf);
              SNetBufDestroy( outbuf);
            }
            break;
        } // switch
      } // if
      else {
        LLSTnext( lst);
      }
    } // while !got_record
  } // while !terminate

  SNetMemFree( inf);
  return( NULL);
}
*/

static bool CompareSortRecords( snet_record_t *rec1, snet_record_t *rec2) {

  bool res;

  if( ( rec1 == NULL) || ( rec2 == NULL)) {  
    if( ( rec1 == NULL) && ( rec2 == NULL)) {
      res = true;
    }
    else {
      res = false;
    }
  }
  else {
    if( ( SNetRecGetLevel( rec1) == SNetRecGetLevel( rec2)) &&
        ( SNetRecGetNum( rec1) == SNetRecGetNum( rec2)))  {
     res = true;
   }
   else {
      res = false;
    }
  }

  return( res);
}



typedef struct {
  snet_buffer_t *buf;
  snet_record_t *current;
} snet_collect_elem_t;

static void *Collector( void *info) {

  dispatch_info_t *inf = (dispatch_info_t*)info;
  snet_buffer_t *outbuf = inf->to_buf;
  snet_buffer_t *current_buf;
  sem_t *sem;
  snet_record_t *rec;
  linked_list_t *lst;
  bool terminate = false;
  bool got_record;
  snet_collect_elem_t *tmp_elem, *elem;

  snet_record_t *current_sort_rec = NULL;
  
  int counter = 0; 
  int sort_end_counter = 0;

  sem = SNetMemAlloc( sizeof( sem_t));
  sem_init( sem, 0, 0);

  SNetBufRegisterDispatcher( inf->from_buf, sem);
  
  elem = SNetMemAlloc( sizeof( snet_collect_elem_t));
  elem->buf = inf->from_buf;
  elem->current = NULL;

  lst = LLSTcreate( elem);

  while( !( terminate)) {
    got_record = false;
    sem_wait( sem);
    while( !( got_record)) {
      current_buf = ((snet_collect_elem_t*) LLSTget( lst))->buf;
      rec = SNetBufShow( current_buf);
      if( rec != NULL) { 
       
        got_record = true;
        
        switch( SNetRecGetDescriptor( rec)) {
          case REC_data:
            tmp_elem = LLSTget( lst);
            if( CompareSortRecords( tmp_elem->current, current_sort_rec)) {
              rec = SNetBufGet( current_buf);
              SNetBufPut( outbuf, rec);
            }
            else {
              sem_post( sem);
              LLSTnext( lst);
            }
                
            //rec = SNetBufGet( current_buf);
            //SNetBufPut( outbuf, rec);
            break;
  
          case REC_sync:
            tmp_elem = LLSTget( lst);
            if( CompareSortRecords( tmp_elem->current, current_sort_rec)) {
             rec = SNetBufGet( current_buf);
             tmp_elem = LLSTget( lst);
             tmp_elem->buf = SNetRecGetBuffer( rec);
             LLSTset( lst, tmp_elem);
             SNetBufRegisterDispatcher( SNetRecGetBuffer( rec), sem);
             SNetRecDestroy( rec);
            }
            else {
              sem_post( sem);
              LLSTnext( lst);
            }
            break;
  
          case REC_collect:
            tmp_elem = LLSTget( lst);
            if( CompareSortRecords( tmp_elem->current, current_sort_rec)) {
              rec = SNetBufGet( current_buf);
             tmp_elem = SNetMemAlloc( sizeof( snet_collect_elem_t));
             tmp_elem->buf = SNetRecGetBuffer( rec);
// !!             tmp_elem->current = NULL;
             if( current_sort_rec == NULL) {
               tmp_elem->current = NULL;
             }
             else {
               tmp_elem->current = SNetRecCreate( REC_sort_begin, 
                                  SNetRecGetLevel( current_sort_rec), SNetRecGetNum( current_sort_rec));
             }
             LLSTadd( lst, tmp_elem);
             SNetBufRegisterDispatcher( SNetRecGetBuffer( rec), sem);
             SNetRecDestroy( rec);
            }
            else {
              sem_post( sem);
              LLSTnext( lst);
            }            
            break;
          case REC_sort_begin:
            tmp_elem = LLSTget( lst);
            if( CompareSortRecords( tmp_elem->current, current_sort_rec)) {
              rec = SNetBufGet( current_buf);
              tmp_elem->current = rec;
              counter += 1;

              if( counter == LLSTgetCount( lst)) {
                current_sort_rec = rec;
                counter = 0;
                SNetBufPut( outbuf, rec);
              }
              else {
                //SNetRecDestroy( rec);
              }
            }
            else {
              sem_post( sem);
              LLSTnext( lst);
            }
            break;
          case REC_sort_end:
            tmp_elem = LLSTget( lst);
            if( CompareSortRecords( tmp_elem->current, current_sort_rec)) {
              rec = SNetBufGet( current_buf);
              sort_end_counter += 1;
              if( sort_end_counter == LLSTgetCount( lst)) {
                SNetBufPut( outbuf, rec);
                sort_end_counter = 0;
              }
              else {
                //SNetRecDestroy( rec);
              }
            }
            else {
              sem_post( sem);
              LLSTnext( lst);
            }
            break;
          case REC_terminate:
            tmp_elem = LLSTget( lst);
            if( CompareSortRecords( tmp_elem->current, current_sort_rec)) {
              rec = SNetBufGet( current_buf);
              tmp_elem = LLSTget( lst);
              SNetBufDestroy( tmp_elem->buf);
              LLSTremoveCurrent( lst);
              SNetMemFree( tmp_elem);
              if( LLSTgetCount( lst) == 0) {
                terminate = true;
                SNetBufPut( outbuf, rec);
               SNetBufBlockUntilEmpty( outbuf);
               SNetBufDestroy( outbuf);
              }
            }
            else {
              sem_post( sem);
              LLSTnext( lst);
            }

            break;
        } // switch
      } // if
      else {
        LLSTnext( lst);
      }
    } // while !got_record
  } // while !terminate

  SNetMemFree( inf);
  return( NULL);
}



static snet_buffer_t *CollectorStartup( snet_buffer_t *initial_buffer, 
                                        bool is_det) {
                                          
  snet_buffer_t *outbuf;
  pthread_t *thread;
  dispatch_info_t *buffers;

  buffers = SNetMemAlloc( sizeof( dispatch_info_t));
  outbuf = SNetBufCreate( BUFFER_SIZE);

  buffers->from_buf = initial_buffer;
  buffers->to_buf = outbuf;


  thread = SNetMemAlloc( sizeof( pthread_t));
  
  if( is_det) {
    pthread_create( thread, NULL, DetCollector, (void*)buffers);
  }
  else {
    pthread_create( thread, NULL, Collector, (void*)buffers);
  }
  pthread_detach( *thread);

  return( outbuf);
}

static snet_buffer_t *CreateCollector( snet_buffer_t *initial_buffer) {	

  return( CollectorStartup( initial_buffer, false));
}

static snet_buffer_t *CreateDetCollector( snet_buffer_t *initial_buffer) { 

  return( CollectorStartup( initial_buffer, true));
}

/* ------------------------------------------------------------------------- */
/*  SNetParallel non-deterministic                                           */
/* ------------------------------------------------------------------------- */



static bool ContainsName( int name, int *names, int num) {
  
  int i;
  bool found;

  found = false;

  for( i=0; i<num; i++) {
    if( names[i] == name) {
      found = true;
      break;
    }
  }

  return( found);
}

static match_count_t *CheckMatch( snet_record_t *rec, snet_variantencoding_t *venc) {

  int i;
  match_count_t *mc;

  mc = SNetMemAlloc( sizeof( match_count_t));

  mc->match_count = 0;
  mc->is_match = true;

  if( ( SNetRecGetNumFields( rec) < SNetTencGetNumFields( venc)) ||
      ( SNetRecGetNumTags( rec) < SNetTencGetNumTags( venc)) ||
      ( SNetRecGetNumBTags( rec) != SNetTencGetNumBTags( venc))) {
    mc->is_match = false;
  }
  else { // is_match is set to value inside the macros
    FIND_NAME_LOOP( SNetRecGetNumFields, SNetTencGetNumFields, SNetRecGetFieldNames, SNetTencGetFieldNames);
    FIND_NAME_LOOP( SNetRecGetNumTags, SNetTencGetNumTags, SNetRecGetTagNames, SNetTencGetTagNames);

    for( i=0; i<SNetRecGetNumBTags( rec); i++) {
       if( !( ContainsName( 
               SNetRecGetBTagNames( rec)[i], 
               SNetTencGetBTagNames( venc), 
               SNetTencGetNumBTags( venc)
               )
             )
           ) {
        mc->is_match = false;
      }
      else {
        mc->match_count += 1;
      }
    }

  }

  return( mc);
}

// Checks for "best match" and decides which buffer to dispatch to
// in case of a draw.
static snet_buffer_t *BestMatch( match_count_t *a, match_count_t *b,
                              snet_buffer_t *buf_a, snet_buffer_t *buf_b) {

  snet_buffer_t *ret_buf;
  
  ret_buf = (a->match_count >= b->match_count) ? buf_a : buf_b;

  return( ret_buf);
}

static void *ParallelBoxThread( void *hndl) {

  snet_handle_t *hnd = (snet_handle_t*) hndl;
  snet_record_t *rec;
  snet_buffer_t *go_buffer = NULL;
  match_count_t *match_a, *match_b;
  bool terminate = false;

  while( !( terminate)) {
    
    rec = SNetBufGet( SNetHndGetInbuffer( hnd));

    switch( SNetRecGetDescriptor( rec)) {
      case REC_data:
        match_a = CheckMatch( rec, SNetTencGetVariant( SNetHndGetType( hnd), 1));
        match_b = CheckMatch( rec, SNetTencGetVariant( SNetHndGetType( hnd), 2));
 
        // this is for testing only and may be deleted once we have
        // a proper type checker
        if( !( match_a->is_match) && !( match_b->is_match)) {
          printf("\n\n ** Fatal Error ** : Record doesn't match! (SNETparallel)\n\n");
          exit( 1);
        }
  
        if( match_a->is_match && match_b->is_match) {
          go_buffer = BestMatch( match_a, match_b, 
                                 SNetHndGetOutbufferA( hnd), SNetHndGetOutbufferB( hnd));
        }
        else if( match_a->is_match) {
          go_buffer = SNetHndGetOutbufferA( hnd);

        } else {
         go_buffer = SNetHndGetOutbufferB( hnd);
        } 
       SNetBufPut( go_buffer, rec); 
       SNetMemFree( match_a); SNetMemFree( match_b);
      break;
      case REC_sync:
        SNetHndSetInbuffer( hnd, SNetRecGetBuffer( rec));
        SNetRecDestroy( rec);
        break;
      case REC_collect:
        printf("\nUnhandled control record, destroying it.\n\n");
        SNetRecDestroy( rec);
        break;
      case REC_sort_begin:
        SNetBufPut( SNetHndGetOutbufferA( hnd), 
            SNetRecCreate( REC_sort_begin, SNetRecGetLevel( rec), SNetRecGetNum( rec)));
        SNetBufPut( SNetHndGetOutbufferB( hnd), rec);
        break;
      case REC_sort_end:
        SNetBufPut( SNetHndGetOutbufferA( hnd), 
            SNetRecCreate( REC_sort_end, SNetRecGetLevel( rec), SNetRecGetNum( rec)));
        SNetBufPut( SNetHndGetOutbufferB( hnd), rec);        
        break;
      case REC_terminate:
        terminate = true;
        SNetBufPut( SNetHndGetOutbufferA( hnd), rec);
        SNetBufPut( SNetHndGetOutbufferB( hnd), rec);
        SNetBufBlockUntilEmpty( SNetHndGetOutbufferA( hnd));
        SNetBufBlockUntilEmpty( SNetHndGetOutbufferB( hnd));
        SNetBufDestroy( SNetHndGetOutbufferA( hnd));
        SNetBufDestroy( SNetHndGetOutbufferB( hnd));
        SNetHndDestroy( hnd);
        break;
    }
  }  

  return( NULL);
}



extern snet_buffer_t *SNetParallel( snet_buffer_t *inbuf, snet_buffer_t* (*box_a)(snet_buffer_t*),
                                   snet_buffer_t* (*box_b)(snet_buffer_t*), snet_typeencoding_t *outtype) {
  snet_handle_t *hnd;
  snet_buffer_t *outbuf;
  snet_buffer_t *transit_a, *transit_b;
  snet_buffer_t *outbuf_a, *outbuf_b;
  pthread_t *box_thread;

  transit_a = SNetBufCreate( BUFFER_SIZE);
  transit_b = SNetBufCreate( BUFFER_SIZE);

  hnd = SNetHndCreate( HND_parallel, inbuf, transit_a, transit_b, outtype);
  
  outbuf_a = (*box_a)( transit_a);
  outbuf_b = (*box_b)( transit_b);

  outbuf = CreateCollector( outbuf_a);
  SNetBufPut( outbuf_a, SNetRecCreate( REC_collect, outbuf_b));

  box_thread = SNetMemAlloc( sizeof( pthread_t));
  pthread_create( box_thread, NULL, ParallelBoxThread, (void*)hnd);
  pthread_detach( *box_thread);
 
  return( outbuf);
}


/* B------------------------------------------------------- */
/* B Det Parallel Playground ------------------------------ */
/* B------------------------------------------------------- */



static void *DetParallelBoxThread( void *hndl) {

  snet_handle_t *hnd = (snet_handle_t*) hndl;
  snet_record_t *rec;
  snet_buffer_t *go_buffer = NULL;
  match_count_t *match_a, *match_b;
  bool terminate = false;
  int counter = 1;


  while( !( terminate)) {
    
    rec = SNetBufGet( SNetHndGetInbuffer( hnd));

    switch( SNetRecGetDescriptor( rec)) {
      case REC_data:
        match_a = CheckMatch( rec, SNetTencGetVariant( SNetHndGetType( hnd), 1));
        match_b = CheckMatch( rec, SNetTencGetVariant( SNetHndGetType( hnd), 2));
 
        // this is for testing only and may be deleted once we have
        // a proper type checker
        if( !( match_a->is_match) && !( match_b->is_match)) {
          printf("\n\n ** Fatal Error ** : Record doesn't match! (SNETparallel)\n\n");
          exit( 1);
        }
  
        if( match_a->is_match && match_b->is_match) {
          go_buffer = BestMatch( match_a, match_b, 
                                 SNetHndGetOutbufferA( hnd), SNetHndGetOutbufferB( hnd));
        }
        else if( match_a->is_match) {
          go_buffer = SNetHndGetOutbufferA( hnd);

        } else {
         go_buffer = SNetHndGetOutbufferB( hnd);
        } 
        
        if( go_buffer == SNetHndGetOutbufferA( hnd)) { /* !!! implement this properly !!! */ 
          SNetBufPut( go_buffer, SNetRecCreate( REC_sort_begin, 0, counter));
          SNetBufPut( go_buffer, rec); 
          SNetBufPut( go_buffer, SNetRecCreate( REC_sort_end, 0, counter));
          SNetBufPut( SNetHndGetOutbufferB( hnd), SNetRecCreate( REC_sort_begin, 0, counter));
          SNetBufPut( SNetHndGetOutbufferB( hnd), SNetRecCreate( REC_sort_end, 0, counter));
        }
        else {
          SNetBufPut( go_buffer, SNetRecCreate( REC_sort_begin, 0, counter));
          SNetBufPut( go_buffer, rec); 
          SNetBufPut( go_buffer, SNetRecCreate( REC_sort_end, 0, counter));
          SNetBufPut( SNetHndGetOutbufferA( hnd), SNetRecCreate( REC_sort_begin, 0, counter));
          SNetBufPut( SNetHndGetOutbufferA( hnd), SNetRecCreate( REC_sort_end, 0, counter));          
        }

       
       counter += 1;
       SNetMemFree( match_a); SNetMemFree( match_b);
      break;
      case REC_sync:
        SNetHndSetInbuffer( hnd, SNetRecGetBuffer( rec));
        SNetRecDestroy( rec);
        break;
      case REC_collect:
        printf("\nUnhandled control record, destroying it.\n\n");
        SNetRecDestroy( rec);
        break;
      case REC_sort_begin:
        SNetRecSetLevel( rec, SNetRecGetLevel( rec) + 1);
        SNetBufPut( SNetHndGetOutbufferA( hnd), SNetRecCreate( REC_sort_begin, 0, counter));
        SNetBufPut( SNetHndGetOutbufferA( hnd), rec);
        SNetBufPut( SNetHndGetOutbufferA( hnd), SNetRecCreate( REC_sort_end, 0, counter));
        SNetBufPut( SNetHndGetOutbufferB( hnd), SNetRecCreate( REC_sort_begin, 0, counter));
        SNetBufPut( SNetHndGetOutbufferB( hnd), SNetRecCreate( REC_sort_end, 0, counter));
        counter += 1;
        break;
      case REC_sort_end:
        SNetRecSetLevel( rec, SNetRecGetLevel( rec) + 1);
        SNetBufPut( SNetHndGetOutbufferA( hnd), SNetRecCreate( REC_sort_begin, 0, counter));
        SNetBufPut( SNetHndGetOutbufferA( hnd), rec);
        SNetBufPut( SNetHndGetOutbufferA( hnd), SNetRecCreate( REC_sort_end, 0, counter));
        SNetBufPut( SNetHndGetOutbufferB( hnd), SNetRecCreate( REC_sort_begin, 0, counter));
        SNetBufPut( SNetHndGetOutbufferB( hnd), SNetRecCreate( REC_sort_end, 0, counter));
        counter += 1;
        break;
      case REC_terminate:

        terminate = true;

        SNetBufPut( SNetHndGetOutbufferA( hnd), SNetRecCreate( REC_sort_begin, 0, counter));
        SNetBufPut( SNetHndGetOutbufferA( hnd), rec);
        // counter += 1;
        SNetBufPut( SNetHndGetOutbufferB( hnd), SNetRecCreate( REC_sort_begin, 0, counter));
        SNetBufPut( SNetHndGetOutbufferB( hnd), rec);         
        
        SNetBufBlockUntilEmpty( SNetHndGetOutbufferA( hnd));
        SNetBufBlockUntilEmpty( SNetHndGetOutbufferB( hnd));
        SNetBufDestroy( SNetHndGetOutbufferA( hnd));
        SNetBufDestroy( SNetHndGetOutbufferB( hnd));
        SNetHndDestroy( hnd);
        break;
    }
  }  

  return( NULL);
}


extern snet_buffer_t *SNetParallelDet( snet_buffer_t *inbuf, snet_buffer_t* (*box_a)(snet_buffer_t*),
                                   snet_buffer_t* (*box_b)(snet_buffer_t*), snet_typeencoding_t *outtype) {
  snet_handle_t *hnd;
  snet_buffer_t *outbuf;
  snet_buffer_t *transit_a, *transit_b;
  snet_buffer_t *outbuf_a, *outbuf_b;
  pthread_t *box_thread;

  transit_a = SNetBufCreate( BUFFER_SIZE);
  transit_b = SNetBufCreate( BUFFER_SIZE);

  hnd = SNetHndCreate( HND_parallel, inbuf, transit_a, transit_b, outtype);
  
  outbuf_a = (*box_a)( transit_a);
  outbuf_b = (*box_b)( transit_b);

  outbuf = CreateDetCollector( outbuf_a);
  SNetBufPut( outbuf_a, SNetRecCreate( REC_sort_begin, 0, 0));
  SNetBufPut( outbuf_a, SNetRecCreate( REC_collect, outbuf_b));
  SNetBufPut( outbuf_a, SNetRecCreate( REC_sort_end, 0, 0));
  SNetBufPut( outbuf_b, SNetRecCreate( REC_sort_begin, 0, 0));
  SNetBufPut( outbuf_b, SNetRecCreate( REC_sort_end, 0, 0));  
  box_thread = SNetMemAlloc( sizeof( pthread_t));
  pthread_create( box_thread, NULL, DetParallelBoxThread, (void*)hnd);
  pthread_detach( *box_thread);
 
  return( outbuf);
}



/* E------------------------------------------------------- */
/* E------------------------------------------------------- */
/* E------------------------------------------------------- */


/* ------------------------------------------------------------------------- */
/*  SNetParallelDet                                                          */
/* ------------------------------------------------------------------------- */



extern snet_buffer_t *SNetParallelDet_BACKUP_( snet_buffer_t *inbuf, snet_buffer_t* (*box_a)(snet_buffer_t*),
                                  snet_buffer_t* (*box_b)(snet_buffer_t*), snet_typeencoding_t *outtype) {





  return( SNetParallel( inbuf, box_a, box_b, outtype));
}


/* ------------------------------------------------------------------------- */
/*  SNetStar                                                                 */
/* ------------------------------------------------------------------------- */


#define FIND_NAME_IN_RECORD( TENCNUM, TENCNAMES, RECNAMES, RECNUM)\
    for( j=0; j<TENCNUM( pat); j++) {\
      if( !( ContainsName( TENCNAMES( pat)[j],\
                           RECNAMES( rec),\
                           RECNUM( rec)))) {\
        is_match = false;\
        break;\
      }\
    }

static bool MatchesExitPattern( snet_record_t *rec, snet_typeencoding_t *patterns) {

  int i,j;
  bool is_match;
  snet_variantencoding_t *pat;
  is_match = true; 

  for( i=0; i<SNetTencGetNumVariants( patterns); i++) {
    pat = SNetTencGetVariant( patterns, i+1);

    FIND_NAME_IN_RECORD( SNetTencGetNumFields, SNetTencGetFieldNames, 
                         SNetRecGetFieldNames, SNetRecGetNumFields);
    if( is_match) {
      FIND_NAME_IN_RECORD( SNetTencGetNumTags, SNetTencGetTagNames, 
                           SNetRecGetTagNames, SNetRecGetNumTags);
      if( is_match) {
        FIND_NAME_IN_RECORD( SNetTencGetNumBTags, SNetTencGetBTagNames, 
                             SNetRecGetBTagNames, SNetRecGetNumBTags);
      }
    }
    if( is_match) {
      break;
    }
  }
  return( is_match);
}


static void *StarBoxThread( void *hndl) {

  snet_handle_t *hnd = (snet_handle_t*)hndl;
  snet_buffer_t* (*box)( snet_buffer_t*);
  snet_buffer_t* (*self)( snet_buffer_t*);
  snet_buffer_t *real_outbuf, *our_outbuf, *starbuf=NULL;
  bool terminate = false;
  snet_typeencoding_t *exit_tags;
  snet_record_t *rec, *current_sort_rec = NULL;

  real_outbuf = SNetHndGetOutbuffer( hnd);
  exit_tags = SNetHndGetType( hnd);
  box = SNetHndGetBoxfunA( hnd);
  self = SNetHndGetBoxfunB( hnd);
   
  our_outbuf = SNetBufCreate( BUFFER_SIZE);

  while( !( terminate)) {
  
    rec = SNetBufGet( SNetHndGetInbuffer( hnd));

    switch( SNetRecGetDescriptor( rec)) {
      case REC_data:
        if( MatchesExitPattern( rec, exit_tags)) {
          SNetBufPut( real_outbuf, rec);
        }
        else {
          if( starbuf == NULL) {
            // register new buffer with dispatcher,
            // starbuf is returned by self, which is SNetStarIncarnate
            starbuf = SNetSerial( our_outbuf, box, self);        
            SNetBufPut( real_outbuf, SNetRecCreate( REC_collect, starbuf));
/*            if( current_sort_rec != NULL) {
              printf("put\n");
              SNetBufPut( our_outbuf, SNetRecCreate( REC_sort_begin, 
                              SNetRecGetLevel( current_sort_rec), 
                              SNetRecGetNum( current_sort_rec)));
            }*/
         }
         SNetBufPut( our_outbuf, rec);
       }
       break;

      case REC_sync:
        SNetHndSetInbuffer( hnd, SNetRecGetBuffer( rec));
        SNetRecDestroy( rec);
        break;

      case REC_collect:
        printf("\nUnhandled control record, destroying it.\n\n");
        SNetRecDestroy( rec);
        break;
      case REC_sort_begin:
        SNetRecDestroy( current_sort_rec);
        current_sort_rec = SNetRecCreate( REC_sort_begin, SNetRecGetLevel( rec),
                                          SNetRecGetNum( rec));
        if( starbuf != NULL) {
          SNetBufPut( our_outbuf, SNetRecCreate( REC_sort_begin,
                                    SNetRecGetLevel( rec),
                                    SNetRecGetNum( rec)));
        }
        SNetBufPut( SNetHndGetOutbuffer( hnd), rec);
        break;
      case REC_sort_end:
        if( starbuf != NULL) {
          SNetBufPut( our_outbuf, SNetRecCreate( REC_sort_end,
                                    SNetRecGetLevel( rec),
                                    SNetRecGetNum( rec)));
          
        }
        SNetBufPut( SNetHndGetOutbuffer( hnd), rec);
        break;
      case REC_terminate:
        terminate = true;
        SNetHndDestroy( hnd);
        if( starbuf != NULL) {
          SNetBufPut( our_outbuf, rec);
        }
        SNetBufPut( real_outbuf, rec);
        SNetBufBlockUntilEmpty( our_outbuf);
        SNetBufDestroy( our_outbuf);
        //real_outbuf will be destroyed by the dispatcher
        break;
    }
  }

  return( NULL);
}

extern snet_buffer_t *SNetStar( snet_buffer_t *inbuf, snet_buffer_t* (*box_a)(snet_buffer_t*),  
                           snet_buffer_t* (*box_b)(snet_buffer_t*), snet_typeencoding_t *type) {

    
  snet_buffer_t *star_outbuf, *dispatch_outbuf;
  snet_handle_t *hnd;
  pthread_t *box_thread;

  star_outbuf = SNetBufCreate( BUFFER_SIZE);
//  BUF_CREATE( star_outbuf, BUFFER_SIZE);
  
  
  hnd = SNetHndCreate( HND_star, inbuf, star_outbuf, box_a, box_b, type, false);
  box_thread = SNetMemAlloc( sizeof( pthread_t));

  pthread_create( box_thread, NULL, StarBoxThread, hnd);
  pthread_detach( *box_thread);

  dispatch_outbuf = CreateCollector( star_outbuf);
  
  return( dispatch_outbuf);
}


extern snet_buffer_t *SNetStarIncarnate( snet_buffer_t *inbuf, snet_buffer_t* (*box_a)(snet_buffer_t*),
                              snet_buffer_t* (*box_b)(snet_buffer_t*), snet_typeencoding_t *type) {

  snet_buffer_t *outbuf;
  snet_handle_t *hnd;
  pthread_t *box_thread;

  outbuf = SNetBufCreate( BUFFER_SIZE);


  hnd = SNetHndCreate( HND_star, inbuf, outbuf, box_a, box_b, type, true);
  
  box_thread = SNetMemAlloc( sizeof( pthread_t));
  pthread_create( box_thread, NULL, StarBoxThread, hnd);
  pthread_detach( *box_thread);

  return( outbuf);
}





/* ------------------------------------------------------------------------- */
/*  SNetStarDet                                                              */
/* ------------------------------------------------------------------------- */

static void *DetStarBoxThread( void *hndl) {

  snet_handle_t *hnd = (snet_handle_t*)hndl;
  snet_buffer_t* (*box)( snet_buffer_t*);
  snet_buffer_t* (*self)( snet_buffer_t*);
  snet_buffer_t *real_outbuf, *our_outbuf, *starbuf=NULL;
  bool terminate = false;
  snet_typeencoding_t *exit_tags;
  snet_record_t *rec;
 
  snet_record_t *sort_begin, *sort_end;
  int counter = 0;


  real_outbuf = SNetHndGetOutbuffer( hnd);
  exit_tags = SNetHndGetType( hnd);
  box = SNetHndGetBoxfunA( hnd);
  self = SNetHndGetBoxfunB( hnd);
   
  our_outbuf = SNetBufCreate( BUFFER_SIZE);

  while( !( terminate)) {
  
    rec = SNetBufGet( SNetHndGetInbuffer( hnd));

    switch( SNetRecGetDescriptor( rec)) {
      case REC_data:
        if( !( SNetHndIsIncarnate( hnd))) { 
          sort_begin = SNetRecCreate( REC_sort_begin, 0, counter);
          sort_end = SNetRecCreate( REC_sort_end, 0, counter);
        
          SNetBufPut( real_outbuf, sort_begin);
          if( starbuf != NULL) {
            SNetBufPut( our_outbuf, sort_begin);
          }
          
          if( MatchesExitPattern( rec, exit_tags)) {
            SNetBufPut( real_outbuf, rec);
          }
          else {
            if( starbuf == NULL) {
              starbuf = SNetSerial( our_outbuf, box, self);        
              SNetBufPut( real_outbuf, SNetRecCreate( REC_collect, starbuf));
              SNetBufPut( our_outbuf, sort_begin); 
            }
            SNetBufPut( our_outbuf, rec);
          }

          SNetBufPut( real_outbuf, sort_end);
          if( starbuf != NULL) {
            SNetBufPut( our_outbuf, sort_end);
          }
          counter += 1;
        }
        else { /* incarnation */
          if( MatchesExitPattern( rec, exit_tags)) {
           SNetBufPut( real_outbuf, rec);
         }
         else {
           if( starbuf == NULL) {
             // register new buffer with dispatcher,
             // starbuf is returned by self, which is SNetStarIncarnate
             starbuf = SNetSerial( our_outbuf, box, self);        
             SNetBufPut( real_outbuf, SNetRecCreate( REC_collect, starbuf));
             SNetBufPut( our_outbuf, sort_begin); /* sort_begin is set in "case REC_sort_xxx" */
          }
          SNetBufPut( our_outbuf, rec);
         }
        }
       break;

      case REC_sync:
        SNetHndSetInbuffer( hnd, SNetRecGetBuffer( rec));
        SNetRecDestroy( rec);
        break;

      case REC_collect:
        printf("\nUnhandled control record, destroying it.\n\n");
        SNetRecDestroy( rec);
        break;
      case REC_sort_end:
      case REC_sort_begin:
        if( !( SNetHndIsIncarnate( hnd))) {
          SNetRecSetLevel( rec, SNetRecGetLevel( rec) + 1);
        }
        SNetBufPut( SNetHndGetOutbuffer( hnd), rec);
        if( starbuf != NULL) {
          SNetBufPut( our_outbuf, rec);
        }
        else {
          if( SNetRecGetDescriptor( rec) == REC_sort_begin) {
            sort_begin = rec;
          }
          else {
            sort_end = rec;
          }
        }
        break;
      case REC_terminate:

        terminate = true;
        
        SNetHndDestroy( hnd);
        if( starbuf != NULL) {
          SNetBufPut( our_outbuf, SNetRecCreate( REC_sort_begin, 0, counter));
          SNetBufPut( our_outbuf, rec);
        }
        SNetBufPut( real_outbuf, SNetRecCreate( REC_sort_begin, 0, counter));
        SNetBufPut( real_outbuf, rec);
        SNetBufBlockUntilEmpty( our_outbuf);
        SNetBufDestroy( our_outbuf);
        //real_outbuf will be destroyed by the dispatcher
        break;
    }
  }

  return( NULL);
}

extern snet_buffer_t *SNetStarDet( snet_buffer_t *inbuf, snet_buffer_t* (*box_a)(snet_buffer_t*),  
                           snet_buffer_t* (*box_b)(snet_buffer_t*), snet_typeencoding_t *type) {

    
  snet_buffer_t *star_outbuf, *dispatch_outbuf;
  snet_handle_t *hnd;
  pthread_t *box_thread;

  star_outbuf = SNetBufCreate( BUFFER_SIZE);
  
  hnd = SNetHndCreate( HND_star, inbuf, star_outbuf, box_a, box_b, type, false);
  box_thread = SNetMemAlloc( sizeof( pthread_t));

  pthread_create( box_thread, NULL, DetStarBoxThread, hnd);
  pthread_detach( *box_thread);

  dispatch_outbuf = CreateDetCollector( star_outbuf);
  
  return( dispatch_outbuf);
}


extern snet_buffer_t *SNetStarDetIncarnate( snet_buffer_t *inbuf, snet_buffer_t* (*box_a)(snet_buffer_t*),
                              snet_buffer_t* (*box_b)(snet_buffer_t*), snet_typeencoding_t *type) {

  snet_buffer_t *outbuf;
  snet_handle_t *hnd;
  pthread_t *box_thread;

  outbuf = SNetBufCreate( BUFFER_SIZE);

  hnd = SNetHndCreate( HND_star, inbuf, outbuf, box_a, box_b, type, true);
  
  box_thread = SNetMemAlloc( sizeof( pthread_t));
  pthread_create( box_thread, NULL, DetStarBoxThread, hnd);
  pthread_detach( *box_thread);

  return( outbuf);
}







/* ------------------------------------------------------------------------- */
/*  SNetSync                                                                 */
/* ------------------------------------------------------------------------- */


#ifdef SYNC_FI_VARIANT_2
static snet_record_t *Merge( snet_record_t **storage, snet_typeencoding_t *patterns, 
						            snet_typeencoding_t *outtype, snet_record_t *rec) {

  int i,j, name, *names;
  snet_variantencoding_t *variant;
  
  for( i=0; i<SNetTencGetNumVariants( patterns); i++) {
    variant = SNetTencGetVariant( patterns, i+1);
    names = SNetTencGetFieldNames( variant);
    for( j=0; j<SNetTencGetNumFields( variant); j++) {
      name = names[j];
      SNetRecAddField( rec, name);
      SNetRecSetField( rec, name, SNetRecGetField( storage[i], name));  
    }
    names = SNetTencGetTagNames( variant);
    for( j=0; j<SNetTencGetNumTags( variant); j++) {
      name = names[j];
      SNetRecAddTag( rec, name);
      SNetRecSetTag( rec, name, SNetRecGetTag( storage[i], name));  
    }
  }                   
  
  return( rec);
}
#endif
#ifdef SYNC_FI_VARIANT_1
static snet_record_t *Merge( snet_record_t **storage, snet_typeencoding_t *patterns, 
                        snet_typeencoding_t *outtype) {

  int i,j;
  snet_record_t *outrec;
  snet_variantencoding_t *outvariant;
  snet_variantencoding_t *pat;
  outvariant = SNetTencCopyVariantEncoding( SNetTencGetVariant( outtype, 1));
  
  outrec = SNetRecCreate( REC_data, outvariant);

  for( i=0; i<SNetTencGetNumVariants( patterns); i++) {
    pat = SNetTencGetVariant( patterns, i+1);
    for( j=0; j<SNetTencGetNumFields( pat); j++) {
      // set value in outrec of field (referenced by its new name) to the
      // according value of the original record (old name)
      if( storage[i] != NULL) {
        void *ptr;
        ptr = SNetRecTakeField( storage[i], SNetTencGetFieldNames( pat)[j]);
        SNetRecSetField( outrec, SNetTencGetFieldNames( pat)[j], ptr);
      }
      for( j=0; j<SNetTencGetNumTags( pat); j++) {
        int tag;
        tag = SNetRecTakeTag( storage[i], SNetTencGetTagNames( pat)[j]);
        SNetRecSetTag( outrec, SNetTencGetTagNames( pat)[j], tag);
      }
    }
  }
 
  
  // flow inherit all tags/fields that are present in storage
  // but not in pattern
  for( i=0; i<SNetTencGetNumVariants( patterns); i++) {
    int *names, num;
    if( storage[i] != NULL) {

      names = SNetRecGetUnconsumedTagNames( storage[i]);
      num = SNetRecGetNumTags( storage[i]);
      for( j=0; j<num; j++) {
        int tag_value;
        if( SNetRecAddTag( outrec, names[j])) { // Don't overwrite existing Tags
          tag_value = SNetRecTakeTag( storage[i], names[j]);
          SNetRecSetTag( outrec, names[j], tag_value);
        }
      }
      SNetMemFree( names);

      names = SNetRecGetUnconsumedFieldNames( storage[i]);
      num = SNetRecGetNumFields( storage[i]);
      for( j=0; j<num; j++) {
        if( SNetRecAddField( outrec, names[j])) {
          SNetRecSetField( outrec, names[j], SNetRecTakeField( storage[i], names[j]));
        }
      }
      SNetMemFree( names);
    }
  }

  return( outrec);
}
#endif


static bool MatchPattern( snet_record_t *rec, snet_variantencoding_t *pat) {

  int i,j, *names;
  bool is_match, found_name;
  is_match = true;
  found_name = false;
  FIND_NAME_IN_PATTERN( SNetTencGetNumTags, SNetRecGetNumTags, 
                        SNetTencGetTagNames, SNetRecGetUnconsumedTagNames);

  if( is_match) {
    FIND_NAME_IN_PATTERN( SNetTencGetNumFields, SNetRecGetNumFields, 
                          SNetTencGetFieldNames, SNetRecGetUnconsumedFieldNames);
  }

  return( is_match);
}



static void *SyncBoxThread( void *hndl) {

  int i;
  int match_cnt=0, new_matches=0;
  int num_patterns;
  bool was_match;
  bool terminate = false;
  snet_handle_t *hnd = (snet_handle_t*) hndl;
  snet_buffer_t *outbuf;
  snet_record_t **storage;
  snet_record_t *rec;
  snet_typeencoding_t *outtype;
  snet_typeencoding_t *patterns;
  
  outbuf = SNetHndGetOutbuffer( hnd);
  outtype = SNetHndGetType( hnd);
  patterns = SNetHndGetPatterns( hnd);
  num_patterns = SNetTencGetNumVariants( patterns);

  storage = SNetMemAlloc( num_patterns * sizeof( snet_record_t*));
  for( i=0; i<num_patterns; i++) {
    storage[i] = NULL;
  }
  
  while( !( terminate)) {
    rec = SNetBufGet( SNetHndGetInbuffer( hnd));

    switch( SNetRecGetDescriptor( rec)) {
      case REC_data:
        new_matches = 0;
      for( i=0; i<num_patterns; i++) {
        if( (storage[i] == NULL) && 
            (MatchPattern( rec, SNetTencGetVariant( patterns, i+1)))) {
          storage[i] = rec;
          was_match = true;
          new_matches += 1;
        }
      }

      if( new_matches == 0) {
        SNetBufPut( outbuf, rec);
      } 
      else {
        match_cnt += new_matches;
       if( match_cnt == num_patterns) {
          #ifdef SYNC_FI_VARIANT_1
          SNetBufPut( outbuf, Merge( storage, patterns, outtype));
          #endif
          #ifdef SYNC_FI_VARIANT_2
          SNetBufPut( outbuf, Merge( storage, patterns, outtype, rec));
          #endif
          SNetMemFree( storage);

          SNetBufPut( outbuf, SNetRecCreate( REC_sync, SNetHndGetInbuffer( hnd)));
          terminate = true;
       }
      }
      break;
    case REC_sync:
        SNetHndSetInbuffer( hnd, SNetRecGetBuffer( rec));
        SNetRecDestroy( rec);

      break;
      case REC_collect:
        printf("\nUnhandled control record, destroying it.\n\n");
        SNetRecDestroy( rec);
        break;
      case REC_sort_begin:
        SNetBufPut( SNetHndGetOutbuffer( hnd), rec);
        break;
      case REC_sort_end:
        SNetBufPut( SNetHndGetOutbuffer( hnd), rec);
        break;
    case REC_terminate:
        printf("SYNC DIED\n");
        terminate = true;
        SNetBufPut( outbuf, rec);
      break;
    }
  }

  SNetBufBlockUntilEmpty( outbuf);
  SNetBufDestroy( outbuf);
  SNetDestroyTypeEncoding( outtype);
  SNetDestroyTypeEncoding( patterns);
  SNetHndDestroy( hnd);

  return( NULL);

}


extern snet_buffer_t *SNetSync( snet_buffer_t *inbuf, snet_typeencoding_t *outtype, 
                          snet_typeencoding_t *patterns ) {

  snet_buffer_t *outbuf;
  snet_handle_t *hnd;
  pthread_t *box_thread;

//  outbuf = SNetBufCreate( BUFFER_SIZE);
  BUF_CREATE( outbuf, BUFFER_SIZE);
  hnd = SNetHndCreate( HND_sync, inbuf, outbuf, outtype, patterns);

  box_thread = SNetMemAlloc( sizeof( pthread_t));

  pthread_create( box_thread, NULL, SyncBoxThread, (void*)hnd);
  pthread_detach( *box_thread);

  return( outbuf);
}
                          




/* ------------------------------------------------------------------------- */
/*  SNetSplit                                                                */
/* ------------------------------------------------------------------------- */




snet_blist_elem_t *FindBufInList( linked_list_t *l, int num) {

  int i;
  snet_blist_elem_t *elem;
  for( i=0; i < LLSTgetCount( l); i++) {
    elem = (snet_blist_elem_t*)LLSTget( l);
    if( elem->num == num) {
      return( elem);
    } 
    else {
      LLSTnext( l);
    }
  }
  return( NULL);
}

static void *SplitBoxThread( void *hndl) {

  int i;
  snet_handle_t *hnd = (snet_handle_t*)hndl;
  snet_buffer_t *initial;
  int ltag, ltag_val, utag, utag_val;
  snet_record_t *rec, *current_sort_rec;
  snet_buffer_t* (*boxfun)( snet_buffer_t*);
  bool terminate = false;
  linked_list_t *repos = NULL;
  snet_blist_elem_t *elem;


  initial = SNetHndGetOutbuffer( hnd);
  boxfun = SNetHndGetBoxfun( hnd);
  ltag = SNetHndGetTagA( hnd);
  utag = SNetHndGetTagB( hnd);

  
  while( !( terminate)) {
    rec = SNetBufGet( SNetHndGetInbuffer( hnd));

    switch( SNetRecGetDescriptor( rec)) {
      case REC_data:
        ltag_val = SNetRecGetTag( rec, ltag);
        utag_val = SNetRecGetTag( rec, utag);
        
        if( repos == NULL) {
          elem = SNetMemAlloc( sizeof( snet_blist_elem_t));
          elem->num = ltag_val;
          elem->buf = SNetBufCreate( BUFFER_SIZE);
          SNetBufPut( initial, SNetRecCreate( REC_collect, boxfun( elem->buf)));
          //SNetBufBlockUntilEmpty( initial);
          repos = LLSTcreate( elem);
        }

        for( i = ltag_val; i <= utag_val; i++) {
          elem = FindBufInList( repos, i);
          if( elem == NULL) {
            elem = SNetMemAlloc( sizeof( snet_blist_elem_t));
            elem->num = i;
            elem->buf = SNetBufCreate( BUFFER_SIZE);
            LLSTadd( repos, elem);
            SNetBufPut( initial, SNetRecCreate( REC_collect, boxfun( elem->buf)));
            //SNetBufBlockUntilEmpty( initial);
          } 
          if( i == utag_val) {
            SNetBufPut( elem->buf, rec); // last rec is not copied.
          }
          else {
            SNetBufPut( elem->buf, RECcopy( rec)); // COPY
          }
        } 
        break;
      case REC_sync:
        SNetHndSetInbuffer( hnd, SNetRecGetBuffer( rec));
        SNetRecDestroy( rec);

        break;
      case REC_collect:
        printf("\nUnhandled control record, destroying it.\n\n");
        SNetRecDestroy( rec);
        break;
      case REC_sort_begin:
        current_sort_rec = rec;
        for( i=0; i<( LLSTgetCount( repos) - 1); i++) {
          elem = LLSTget( repos);
          SNetBufPut( elem->buf, 
              SNetRecCreate( REC_sort_begin, SNetRecGetLevel( rec), 
                             SNetRecGetNum( rec)));
          LLSTnext( repos);
        }
        elem = LLSTget( repos);
        SNetBufPut( elem->buf, rec);
        break;
      case REC_sort_end:
        current_sort_rec = rec;
        for( i=0; i<( LLSTgetCount( repos) - 1); i++) {
          elem = LLSTget( repos);
          SNetBufPut( elem->buf, 
              SNetRecCreate( REC_sort_end, SNetRecGetLevel( rec), 
                             SNetRecGetNum( rec)));
          LLSTnext( repos);
        }
        elem = LLSTget( repos);
        SNetBufPut( elem->buf, rec);      
        break;
      case REC_terminate:
        terminate = true;

        for( i=0; i<LLSTgetCount( repos); i++) {
          elem = LLSTget( repos);
          SNetBufPut( (snet_buffer_t*)elem->buf, rec);
          LLSTnext( repos);
        }
        SNetBufPut( initial, rec);
        break;
    }
  }

  return( NULL);
}



extern snet_buffer_t *SNetSplit( snet_buffer_t *inbuf, snet_buffer_t* (*box_a)( snet_buffer_t*),
                            int ltag, int utag) {

  snet_buffer_t *initial, *outbuf;
  snet_handle_t *hnd;
  pthread_t *box_thread;

  initial = SNetBufCreate( BUFFER_SIZE);
  hnd = SNetHndCreate( HND_split, inbuf, initial, box_a, ltag, utag);
 
  outbuf = CreateCollector( initial);

  box_thread = SNetMemAlloc( sizeof( pthread_t));
  pthread_create( box_thread, NULL, SplitBoxThread, (void*)hnd);
  pthread_detach( *box_thread);

  return( outbuf);
}



/* B------------------------------------------------------- */
/* B Det Split    Playground ------------------------------ */
/* B------------------------------------------------------- */


static void *DetSplitBoxThread( void *hndl) {

  int i,j;
  snet_handle_t *hnd = (snet_handle_t*)hndl;
  snet_buffer_t *initial, *tmp;
  int ltag, ltag_val, utag, utag_val;
  snet_record_t *rec;
  snet_buffer_t* (*boxfun)( snet_buffer_t*);
  bool terminate = false;
  linked_list_t *repos = NULL;
  snet_blist_elem_t *elem;

  int counter = 1;


  initial = SNetHndGetOutbuffer( hnd);
  boxfun = SNetHndGetBoxfun( hnd);
  ltag = SNetHndGetTagA( hnd);
  utag = SNetHndGetTagB( hnd);

  
  while( !( terminate)) {
    rec = SNetBufGet( SNetHndGetInbuffer( hnd));

    switch( SNetRecGetDescriptor( rec)) {
      case REC_data:
        ltag_val = SNetRecGetTag( rec, ltag);
        utag_val = SNetRecGetTag( rec, utag);
        
        if( repos == NULL) {
          elem = SNetMemAlloc( sizeof( snet_blist_elem_t));
          elem->num = ltag_val;
          elem->buf = SNetBufCreate( BUFFER_SIZE);
          SNetBufPut( initial, SNetRecCreate( REC_sort_begin, 0, 0));
          
          tmp = boxfun( elem->buf);
          SNetBufPut( initial, SNetRecCreate( REC_collect, tmp));
          SNetBufPut( tmp, SNetRecCreate( REC_sort_begin, 0, 0));
          SNetBufPut( tmp, SNetRecCreate( REC_sort_end, 0, 0));

          SNetBufPut( initial, SNetRecCreate( REC_sort_end, 0, 0));
          repos = LLSTcreate( elem);
        }


        SNetBufPut( initial, SNetRecCreate( REC_sort_begin, 0, counter)); 
        for( j=0; j<LLSTgetCount( repos); j++) {
          elem = LLSTget( repos);
          SNetBufPut( elem->buf, SNetRecCreate( REC_sort_begin, 0, counter)); 
          LLSTnext( repos);
        }

        for( i = ltag_val; i <= utag_val; i++) {
          elem = FindBufInList( repos, i);
          if( elem == NULL) {
            elem = SNetMemAlloc( sizeof( snet_blist_elem_t));
            elem->num = i;
            elem->buf = SNetBufCreate( BUFFER_SIZE);
            LLSTadd( repos, elem);
            SNetBufPut( initial, SNetRecCreate( REC_collect, boxfun( elem->buf)));
            SNetBufPut( elem->buf, SNetRecCreate( REC_sort_begin, 0, counter));
          }
          if( i == utag_val) {
            SNetBufPut( elem->buf, rec); // last rec is not copied.
          }
          else {
            SNetBufPut( elem->buf, RECcopy( rec)); // COPY
          }
        }

        for( j=0; j<LLSTgetCount( repos); j++) {
          elem = LLSTget( repos);
          SNetBufPut( elem->buf, SNetRecCreate( REC_sort_end, 0, counter)); 
          LLSTnext( repos);
        }
        SNetBufPut( initial, SNetRecCreate( REC_sort_end, 0, counter)); 
        counter += 1;

        break;
      case REC_sync:
        SNetHndSetInbuffer( hnd, SNetRecGetBuffer( rec));
        SNetRecDestroy( rec);
        break;
      case REC_collect:
        printf("\nUnhandled control record, destroying it.\n\n");
        SNetRecDestroy( rec);
        break;
      case REC_sort_begin:
        SNetRecSetLevel( rec, SNetRecGetLevel( rec) + 1);
        SNetBufPut( SNetHndGetOutbuffer( hnd), rec);
        break;
      case REC_sort_end:
        SNetRecSetLevel( rec, SNetRecGetLevel( rec) + 1);
        SNetBufPut( SNetHndGetOutbuffer( hnd), rec);
        break;
      case REC_terminate:
        terminate = true;

        for( i=0; i<LLSTgetCount( repos); i++) {
          elem = LLSTget( repos);
          SNetBufPut( elem->buf, SNetRecCreate( REC_sort_begin, 0, counter)); 
          SNetBufPut( (snet_buffer_t*)elem->buf, rec);
          LLSTnext( repos);
        }
        SNetBufPut( initial, SNetRecCreate( REC_sort_begin, 0, counter)); 
        SNetBufPut( initial, rec);
        break;
    }
  }

  return( NULL);
}




extern snet_buffer_t *SNetSplitDet( snet_buffer_t *inbuf, snet_buffer_t* (*box_a)( snet_buffer_t*),
                            int ltag, int utag) {

  snet_buffer_t *initial, *outbuf;
  snet_handle_t *hnd;
  pthread_t *box_thread;

  initial = SNetBufCreate( BUFFER_SIZE);
  hnd = SNetHndCreate( HND_split, inbuf, initial, box_a, ltag, utag);
 
  outbuf = CreateDetCollector( initial);

  box_thread = SNetMemAlloc( sizeof( pthread_t));
  pthread_create( box_thread, NULL, DetSplitBoxThread, (void*)hnd);
  pthread_detach( *box_thread);

  return( outbuf);
}

/* E------------------------------------------------------- */
/* E Det Split    Playground ------------------------------ */
/* E------------------------------------------------------- */



/* ------------------------------------------------------------------------- */
/*  SNetFilter                                                               */
/* ------------------------------------------------------------------------- */


extern snet_filter_instruction_t *SNetCreateFilterInstruction( snet_filter_opcode_t opcode, ...) {

  va_list args;
  snet_filter_instruction_t *instr;

  instr = SNetMemAlloc( sizeof( snet_filter_instruction_t));
  instr->opcode = opcode;

  va_start( args, opcode);   

  switch( opcode) {
    case FLT_add_tag:
    case FLT_strip_field:
    case FLT_strip_tag:
    case FLT_use_field:
    case FLT_use_tag:
      instr->data = SNetMemAlloc( sizeof( int));
      instr->data[0] = va_arg( args, int);
      break;
    case FLT_set_tag:
    case FLT_rename_tag:
    case FLT_rename_field:
      instr->data = SNetMemAlloc( 2 * sizeof( int));
      instr->data[0] = va_arg( args, int);
      instr->data[1] = va_arg( args, int);
      break;
    default:
      printf("\n ** Fatal Error ** : Filter operation (%d) not implemented.\n\n", opcode);
      exit( 1);
  }
  
  va_end( args);

  
  return( instr);  
}


extern void SNetDestroyFilterInstruction( snet_filter_instruction_t *instr) {
  
  SNetMemFree( instr->data);
  SNetMemFree( instr);
}

extern snet_filter_instruction_set_t *SNetCreateFilterInstructionSet( int num, ...) {

  va_list args;
  int i;
  snet_filter_instruction_set_t *set;

  set = SNetMemAlloc( sizeof( snet_filter_instruction_set_t));

  set->num = num;
  set->instructions = SNetMemAlloc( num * sizeof( snet_filter_instruction_t*));

  va_start( args, num);

  for( i=0; i<num; i++) {
    set->instructions[i] = va_arg( args, snet_filter_instruction_t*);
  }

  va_end( args);

  return( set);
}




extern void SNetDestroyFilterInstructionSet( snet_filter_instruction_set_t *set) {

  int i;

  for( i=0; i<set->num; i++) {
    SNetDestroyFilterInstruction( set->instructions[i]);
  }

  SNetMemFree( set->instructions);
  SNetMemFree( set);
}


static void *FilterThread( void *hndl) {

  int i,j;
  snet_variantencoding_t *names;
  snet_handle_t *hnd = (snet_handle_t*) hndl;
  snet_buffer_t *outbuf;
  snet_typeencoding_t *out_type;
  snet_filter_instruction_set_t **instructions, *set;
  snet_filter_instruction_t *instr;
  snet_variantencoding_t *variant;
  snet_record_t *out_rec, *in_rec;
  bool terminate = false;

  outbuf = SNetHndGetOutbuffer( hnd);
//  in_variant = SNetTencGetVariant( SNetHndGetInType( hnd), 1);
  out_type =  SNetHndGetOutType( hnd);

  instructions = SNetHndGetFilterInstructions( hnd);

  while( !( terminate)) {
    in_rec = SNetBufGet( SNetHndGetInbuffer( hnd));

    switch( SNetRecGetDescriptor( in_rec)) {
      case REC_data:
        for( i=0; i<SNetTencGetNumVariants( out_type); i++) {
        names = SNetTencCopyVariantEncoding( SNetRecGetVariantEncoding( in_rec));
        variant = SNetTencCopyVariantEncoding( SNetTencGetVariant( out_type, i+1));
        out_rec = SNetRecCreate( REC_data, variant);
        set = instructions[i];
        // this runs for each filter instruction 
        for( j=0; j<set->num; j++) {
          instr = set->instructions[j];
          switch( instr->opcode) {
            case FLT_strip_tag:
              SNetTencRemoveTag( names, instr->data[0]);
              break;
            case FLT_strip_field:
              SNetTencRemoveField( names, instr->data[0]);
              break;
            case FLT_add_tag:
              SNetRecSetTag( out_rec, instr->data[0], 0); 
              break;
            case FLT_set_tag:
              SNetRecSetTag( out_rec, instr->data[0], instr->data[1]);
              break;
            case FLT_rename_tag:
              SNetRecSetTag( out_rec, instr->data[1], 
                  SNetRecGetTag( in_rec, instr->data[0]));
              SNetTencRemoveTag( names, instr->data[0]);
              break;
            case FLT_rename_field:
              SNetRecSetField( out_rec, instr->data[1], 
                  SNetRecGetField( in_rec, instr->data[0]));
              SNetTencRemoveField( names, instr->data[0]);
              break;
            case FLT_copy_field:
              break;
            case FLT_use_tag:
              SNetRecSetTag( out_rec, instr->data[0],
                  SNetRecGetTag( in_rec, instr->data[0]));
              break;
            case FLT_use_field:
              SNetRecSetField( out_rec, instr->data[0],
                  SNetRecGetField( in_rec, instr->data[0]));
              break;
          }
        }
        // flow inherit, remove everthing that is already present in the 
        // outrecord. Renamed fields/tags are removed above.
        for( j=0; j<SNetTencGetNumFields( variant); j++) {
          SNetTencRemoveField( names, SNetTencGetFieldNames( variant)[j]);
        }
        for( j=0; j<SNetTencGetNumTags( variant); j++) {
          SNetTencRemoveTag( names, SNetTencGetTagNames( variant)[j]);
        }
  
       // add everything that is left.
       for( j=0; j<SNetTencGetNumFields( names); j++) {
         if( SNetRecAddField( out_rec, SNetTencGetFieldNames( names)[j])) {
            SNetRecSetField( out_rec, 
                SNetTencGetFieldNames( names)[j],
                SNetRecGetField( in_rec, SNetTencGetFieldNames( names)[j]));
         }
       }  
      for( j=0; j<SNetTencGetNumTags( names); j++) {
         if( SNetRecAddTag( out_rec, SNetTencGetTagNames( names)[j])) {
            SNetRecSetTag( out_rec, 
                SNetTencGetTagNames( names)[j],
                SNetRecGetTag( in_rec, SNetTencGetTagNames( names)[j]));
         }
      }  

      SNetTencDestroyVariantEncoding( names);  
      SNetBufPut( outbuf, out_rec);
      }
      SNetRecDestroy( in_rec);
      break;
    case REC_sync:
        SNetHndSetInbuffer( hnd, SNetRecGetBuffer( in_rec));
        SNetRecDestroy( in_rec);
      break;
      case REC_collect:
        printf("\nUnhandled control record, destroying it.\n\n");
        SNetRecDestroy( in_rec);
        break;
      case REC_sort_begin:
        SNetBufPut( SNetHndGetOutbuffer( hnd), in_rec);
        break;
      case REC_sort_end:
        SNetBufPut( SNetHndGetOutbuffer( hnd), in_rec);
        break;
    case REC_terminate:
      terminate = true;
      SNetBufPut( outbuf, in_rec);
      SNetBufBlockUntilEmpty( outbuf);
      SNetBufDestroy( outbuf);
      SNetHndDestroy( hnd);
      break;
    }
  }  

  return( NULL);
}




extern snet_buffer_t *SNetFilter( snet_buffer_t *inbuf, snet_typeencoding_t *in_type, 
                             snet_typeencoding_t *out_type, ...) {
  int i;
  va_list args;
  snet_buffer_t *outbuf;
  snet_handle_t *hnd;
  pthread_t *box_thread;

  snet_filter_instruction_set_t **set;

  set = SNetMemAlloc( SNetTencGetNumVariants( out_type) * sizeof( snet_filter_instruction_set_t*));

  va_start( args, out_type);
  for( i=0; i<SNetTencGetNumVariants( out_type); i++) {
    set[i] = va_arg( args, snet_filter_instruction_set_t*);
  }
  va_end( args);

//  outbuf = SNetBufCreate( BUFFER_SIZE);
  BUF_CREATE( outbuf, BUFFER_SIZE);
  hnd = SNetHndCreate( HND_filter, inbuf, outbuf, in_type, out_type, set);

  box_thread = SNetMemAlloc( sizeof( pthread_t));
  pthread_create( box_thread, NULL, FilterThread, (void*)hnd);
  pthread_detach( *box_thread);

  return( outbuf);
}