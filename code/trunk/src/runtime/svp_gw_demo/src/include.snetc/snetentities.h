/*----------------------------------------------------------------------------*/
/*
      -------------------------------------------------------------------

           * * * * ! SVP S-Net Graph Walker Runtime (demo) ! * * * *

                   Computer Systems Architecture (CSA) Group
                             Informatics Institute
                         University Of Amsterdam  2008
                         
      -------------------------------------------------------------------

    File Name      : snetentities.h

    File Type      : Header File

    ---------------------------------------

    File 
    Description    : Header included from the S-Net compiler 
                     generated code as a master header for the
                     runtime.

    Updates 
    Description    : N/A

*/
/*----------------------------------------------------------------------------*/

#ifndef __SVPSNETGWRT_SNET_GWRT_SNETC_SNETENTITIES_H
#define __SVPSNETGWRT_SNET_GWRT_SNETC_SNETENTITIES_H

#include "snet-gwrt.utc.h"

/*----------------------------------------------------------------------------*/

#define field 0
#define tag   1
#define btag  2

/*---*/

#define REC_data      REC_DESCR_DATA
#define REC_terminate REC_DESCR_CTRL

#define MODE_textual  REC_DATA_MODE_TXT
#define MODE_binary   REC_DATA_MODE_BIN

/*---*/

#define snet_tag      FILTER_OP_TAG
#define snet_btag     FILTER_OP_BTAG
#define snet_field    FILTER_OP_FIELD,
#define create_record FILTER_OP_CREATE_REC

/*---*/

#define SNetEcreateList      SNetEListCreate
#define SNetTencCreateVector SNetTencVectorCreate

/*----------------------------------------------------------------------------*/
/**
 * Datatype representing a stream that is
 * used by the S-Net compiler as input for the
 * "entities" functions of the CI. For us this
 * is also "snet_gnode_t". 
 *
 * It is required so that no changes are needed in 
 * the compiler generated code.
 */
typedef snet_gnode_t snet_tl_stream_t;

#endif // __SVPSNETGWRT_SNET_GWRT_SNETC_SNETENTITIES_H

/*------------------------------- END OF FILE --------------------------------*/
/*----------------------------------------------------------------------------*/
