#pragma once

/// @file Opcodes.h
/// @brief ED2K/Kad protocol constants — replaces MFC Opcodes.h.
///
/// All Windows-specific constructs (_T(), ui64 suffix, _UI32_MAX) are replaced
/// with portable C++23 equivalents. Numeric constants and protocol opcodes are
/// preserved exactly as in the original.

#include <cinttypes>
#include <climits>
#include <cstdint>
#include <ctime>

// ---------------------------------------------------------------------------
// Protocol versions
// ---------------------------------------------------------------------------

// MOD Note: Do not change this part - Merkur
#define EMULE_PROTOCOL                  0x01
// MOD Note: end
#define EDONKEYVERSION                  0x3C
#define KADEMLIA_VERSION1_46c           0x01 /*45b - 46c*/
#define KADEMLIA_VERSION2_47a           0x02 /*47a*/
#define KADEMLIA_VERSION3_47b           0x03 /*47b*/
#define KADEMLIA_VERSION4_47c           0x04 /*47c*/
#define KADEMLIA_VERSION5_48a           0x05 // -0.48a
#define KADEMLIA_VERSION6_49aBETA       0x06 // -0.49aBETA1
#define KADEMLIA_VERSION7_49a           0x07 // -0.49a
#define KADEMLIA_VERSION8_49b           0x08 // TAG_KADMISCOPTIONS, KADEMLIA2_HELLO_RES_ACK
#define KADEMLIA_VERSION9_50a           0x09 // handling AICH hashes on keyword storage
#define KADEMLIA_VERSION                0x0a // -Change CT_EMULE_MISCOPTIONS2 if Kad version becomes >= 15 (0x0F)
#define PREFFILE_VERSION                0x14 //<<-- last change: reduced .dat, by using .ini
#define PARTFILE_VERSION                0xe0
#define PARTFILE_SPLITTEDVERSION        0xe1
#define PARTFILE_VERSION_LARGEFILE      0xe2
#define SOURCEEXCHANGE2_VERSION         4    // replaces the version sent in MISC_OPTIONS flag from SX1

#define CREDITFILE_VERSION              0x12
#define CREDITFILE_VERSION_29           0x11
#define COMPILE_DATE                    __DATE__
#define COMPILE_TIME                    __TIME__

#ifdef NDEBUG
#define EMULE_GUID                      "EMULE-{4EADC6FC-516F-4b7c-9066-97D893649570}"
#else
#define EMULE_GUID                      "EMULE-{4EADC6FC-516F-4b7c-9066-97D893649570}-DEBUG"
#endif

// ---------------------------------------------------------------------------
// Time conversion macros
// ---------------------------------------------------------------------------

#define SEC(sec)        (sec)
#define MIN2S(min)      ((min)*60)
#define HR2S(hr)        MIN2S((hr)*60)
#define DAY2S(day)      HR2S((day)*24)
#define SEC2MS(sec)     ((sec)*1000)
#define MIN2MS(min)     SEC2MS(MIN2S(min))
#define HR2MS(hr)       SEC2MS(HR2S(hr))
#define DAY2MS(day)     SEC2MS(DAY2S(day))

// ---------------------------------------------------------------------------
// Timing constants
// ---------------------------------------------------------------------------

// MOD Note: Do not change this part - Merkur
#define UDPSEARCHSPEED              SEC2MS(1)   //1 sec
#define MAX_RESULTS                 100
#define MAX_MORE_SEARCH_REQ         5
#define MAX_CLIENTCONNECTIONTRY     2
#define CONNECTION_TIMEOUT          SEC2MS(40)  //40 secs
#define FILEREASKTIME               MIN2MS(29)  //29 mins
#define SERVERREASKTIME             MIN2MS(15)  //15 mins
#define UDPSERVERREASKTIME          MIN2MS(30)  //30 mins
#define MAX_SERVERFAILCOUNT         10
#define SOURCECLIENTREASKS          MIN2MS(40)  //40 mins
#define SOURCECLIENTREASKF          MIN2MS(5)   //5 mins
#define KADEMLIAASKTIME             SEC2MS(1)   //1 second
#define KADEMLIATOTALFILE           5
#define KADEMLIAREASKTIME           HR2MS(1)    //1 hour
#define KADEMLIAPUBLISHTIME         SEC(2)      //2 second
#define KADEMLIATOTALSTORENOTES     1
#define KADEMLIATOTALSTORESRC       4
#define KADEMLIATOTALSTOREKEY       3
#define KADEMLIAREPUBLISHTIMES      HR2S(5)     //5 hours
#define KADEMLIAREPUBLISHTIMEN      HR2S(24)    //24 hours
#define KADEMLIAREPUBLISHTIMEK      HR2S(24)    //24 hours
#define KADEMLIADISCONNECTDELAY     MIN2S(20)   //20 mins
#define KADEMLIAMAXINDEX            50000
#define KADEMLIAMAXENTRIES          60000
#define KADEMLIAMAXSOURCEPERFILE    1000
#define KADEMLIAMAXNOTESPERFILE     150
#define KADEMLIAFIREWALLCHECKS      4

#define ED2KREPUBLISHTIME           MIN2MS(1)   //1 min
#define MINCOMMONPENALTY            4
#define UDPSERVERSTATTIME           SEC2MS(5)   //5 secs
#define UDPSERVSTATREASKTIME        (static_cast<time_t>(HR2S(4.5))) //4.5 hours
#define UDPSERVSTATMINREASKTIME     MIN2S(20)   //minimum time between two pings
#define UDPSERVERPORT               4665
#define UDPMAXQUEUETIME             SEC2MS(30)  //30 Seconds
#define RSAKEYSIZE                  384         //384 bits
#define MAX_SOURCES_FILE_SOFT       750
#define MAX_SOURCES_FILE_UDP        50u
#define SESSIONMAXTRANS             (PARTSIZE+20*1024)
#define SESSIONMAXTIME              HR2MS(1)    //1 hour
#define MAXFILECOMMENTLEN           128
#define PARTSIZE                    UINT64_C(9728000)
#define MAX_EMULE_FILE_SIZE         UINT64_C(0x4000000000)  // = 2^38 = 256GB
#define OLD_MAX_EMULE_FILE_SIZE     UINT64_C(4290048000)    // ~4GB
// MOD Note: end

#define CONFIGFOLDER                "config/"
#define MAXCONPER5SEC               20
#define MAXCON5WIN9X                10
#define UPLOAD_CLIENT_MAXDATARATE   (25*1024)
#define MIN_UP_CLIENTS_ALLOWED      2
#define MAX_UP_CLIENTS_ALLOWED      100
#define DOWNLOADTIMEOUT             SEC2MS(100)
#define CONSERVTIMEOUT              SEC2MS(25)
#define RARE_FILE                   50
#define BADCLIENTBAN                4
#define MIN_REQUESTTIME             MIN2MS(10)
#define MAX_PURGEQUEUETIME          HR2MS(1)
#define PURGESOURCESWAPSTOP         MIN2MS(15)  // 15 min
#define CONNECTION_LATENCY          22050
#define MINWAIT_BEFORE_DLDISPLAY_WINDOWUPDATE   SEC2MS(1)
#define MINWAIT_BEFORE_ULDISPLAY_WINDOWUPDATE   SEC2MS(1)
#define CLIENTBANTIME               HR2MS(2)    // 2h
#define TRACKED_CLEANUP_TIME        HR2MS(1)    // 1 hour
#define KEEPTRACK_TIME              HR2MS(2)    // 2h
#define LOCALSERVERREQUESTS         SEC2MS(20)
#define DISKSPACERECHECKTIME        MIN2MS(15)
#define CLIENTLIST_CLEANUP_TIME     MIN2MS(34)  // 34 min
#define MAXPRIORITYCOLL_SIZE        (50*1024)
#define SEARCH_SPAM_THRESHOLD       60
#define OLDFILES_PARTIALLYPURGE     DAY2S(31)

// ---------------------------------------------------------------------------
// Protocol headers and structural constants
// ---------------------------------------------------------------------------

#define UDP_KAD_MAXFRAGMENT         1420
#define EMBLOCKSIZE                 184320u
#define OP_EDONKEYHEADER            0xE3
#define OP_KADEMLIAHEADER           0xE4
#define OP_KADEMLIAPACKEDPROT       0xE5
#define OP_EDONKEYPROT              OP_EDONKEYHEADER
#define OP_PACKEDPROT               0xD4
#define OP_EMULEPROT                0xC5
#define OP_UDPRESERVEDPROT1         0xA3
#define OP_UDPRESERVEDPROT2         0xB2
#define OP_MLDONKEYPROT             0x00
#define MET_HEADER                  0x0E
#define MET_HEADER_I64TAGS          0x0F

#define UNLIMITED                   UINT32_MAX

// Proxy types
#define PROXYTYPE_NOPROXY           0
#define PROXYTYPE_SOCKS4            1
#define PROXYTYPE_SOCKS4A           2
#define PROXYTYPE_SOCKS5            3
#define PROXYTYPE_HTTP10            4
#define PROXYTYPE_HTTP11            5

// ---------------------------------------------------------------------------
// Client <-> Server opcodes
// ---------------------------------------------------------------------------

#define OP_LOGINREQUEST             0x01
#define OP_REJECT                   0x05
#define OP_GETSERVERLIST            0x14
#define OP_OFFERFILES               0x15
#define OP_SEARCHREQUEST            0x16
#define OP_DISCONNECT               0x18
#define OP_GETSOURCES               0x19
#define OP_SEARCH_USER              0x1A
#define OP_CALLBACKREQUEST          0x1C
#define OP_QUERY_CHATS              0x1D    // deprecated
#define OP_CHAT_MESSAGE             0x1E    // deprecated
#define OP_JOIN_ROOM                0x1F    // deprecated
#define OP_QUERY_MORE_RESULT        0x21
#define OP_SERVERLIST               0x32
#define OP_SEARCHRESULT             0x33
#define OP_SERVERSTATUS             0x34
#define OP_CALLBACKREQUESTED        0x35
#define OP_CALLBACK_FAIL            0x36
#define OP_SERVERMESSAGE            0x38
#define OP_CHAT_ROOM_REQUEST        0x39    // deprecated
#define OP_CHAT_BROADCAST           0x3A    // deprecated
#define OP_CHAT_USER_JOIN           0x3B    // deprecated
#define OP_CHAT_USER_LEAVE          0x3C    // deprecated
#define OP_CHAT_USER                0x3D    // deprecated
#define OP_IDCHANGE                 0x40
#define OP_SERVERIDENT              0x41
#define OP_FOUNDSOURCES             0x42
#define OP_USERS_LIST               0x43
#define OP_GETSOURCES_OBFU          0x23
#define OP_FOUNDSOURCES_OBFU        0x44

// ---------------------------------------------------------------------------
// Client <-> UDP server opcodes
// ---------------------------------------------------------------------------

#define OP_GLOBSEARCHREQ3           0x90
#define OP_GLOBSEARCHREQ2           0x92
#define OP_GLOBSERVSTATREQ          0x96
#define OP_GLOBSERVSTATRES          0x97
#define OP_GLOBSEARCHREQ            0x98
#define OP_GLOBSEARCHRES            0x99
#define OP_GLOBGETSOURCES           0x9A
#define OP_GLOBGETSOURCES2          0x94
#define OP_GLOBFOUNDSOURCES         0x9B
#define OP_GLOBCALLBACKREQ          0x9C
#define OP_INVALID_LOWID            0x9E
#define OP_SERVER_LIST_REQ          0xA0
#define OP_SERVER_LIST_RES          0xA1
#define OP_SERVER_DESC_REQ          0xA2
#define OP_SERVER_DESC_RES          0xA3
#define OP_SERVER_LIST_REQ2         0xA4

#define INV_SERV_DESC_LEN           0xF0FF

// ---------------------------------------------------------------------------
// Client <-> Client opcodes
// ---------------------------------------------------------------------------

#define OP_HELLO                    0x01
#define OP_SENDINGPART              0x46
#define OP_REQUESTPARTS             0x47
#define OP_FILEREQANSNOFIL          0x48
#define OP_END_OF_DOWNLOAD          0x49
#define OP_ASKSHAREDFILES           0x4A
#define OP_ASKSHAREDFILESANSWER     0x4B
#define OP_HELLOANSWER              0x4C
#define OP_CHANGE_CLIENT_ID         0x4D
#define OP_MESSAGE                  0x4E
#define OP_SETREQFILEID             0x4F
#define OP_FILESTATUS               0x50
#define OP_HASHSETREQUEST           0x51    // *DEPRECATED*
#define OP_HASHSETANSWER            0x52    // *DEPRECATED*
#define OP_STARTUPLOADREQ           0x54
#define OP_ACCEPTUPLOADREQ          0x55
#define OP_CANCELTRANSFER           0x56
#define OP_OUTOFPARTREQS            0x57
#define OP_REQUESTFILENAME          0x58
#define OP_REQFILENAMEANSWER        0x59
#define OP_CHANGE_SLOT              0x5B
#define OP_QUEUERANK                0x5C
#define OP_ASKSHAREDDIRS            0x5D
#define OP_ASKSHAREDFILESDIR        0x5E
#define OP_ASKSHAREDDIRSANS         0x5F
#define OP_ASKSHAREDFILESDIRANS     0x60
#define OP_ASKSHAREDDENIEDANS       0x61

// Shared file identifiers (protocol strings from eDonkeyHybrid)
#define OP_INCOMPLETE_SHARED_FILES  "!Incomplete Files"
#define OP_OTHER_SHARED_FILES       "!Other"

// Message length limits
#define MAX_CLIENT_MSG_LEN          450
#define MAX_IRC_MSG_LEN             450

// ---------------------------------------------------------------------------
// Extended protocol: Client <-> Client opcodes
// ---------------------------------------------------------------------------

#define OP_EMULEINFO                0x01
#define OP_EMULEINFOANSWER          0x02
#define OP_COMPRESSEDPART           0x40
#define OP_QUEUERANKING             0x60
#define OP_FILEDESC                 0x61
#define OP_REQUESTSOURCES           0x81    // *DEPRECATED*
#define OP_ANSWERSOURCES            0x82    // *DEPRECATED*
#define OP_REQUESTSOURCES2          0x83
#define OP_ANSWERSOURCES2           0x84
#define OP_PUBLICKEY                0x85
#define OP_SIGNATURE                0x86
#define OP_SECIDENTSTATE            0x87
#define OP_REQUESTPREVIEW           0x90
#define OP_PREVIEWANSWER            0x91
#define OP_MULTIPACKET              0x92    // *DEPRECATED*
#define OP_MULTIPACKETANSWER        0x93    // *DEPRECATED*
#define OP_PEERCACHE_QUERY          0x94    // *DEFUNCT*
#define OP_PEERCACHE_ANSWER         0x95    // *DEFUNCT*
#define OP_PEERCACHE_ACK            0x96    // *DEFUNCT*
#define OP_PUBLICIP_REQ             0x97
#define OP_PUBLICIP_ANSWER          0x98
#define OP_CALLBACK                 0x99
#define OP_REASKCALLBACKTCP         0x9A
#define OP_AICHREQUEST              0x9B
#define OP_AICHANSWER               0x9C
#define OP_AICHFILEHASHANS          0x9D    // *DEPRECATED*
#define OP_AICHFILEHASHREQ          0x9E    // *DEPRECATED*
#define OP_BUDDYPING                0x9F
#define OP_BUDDYPONG                0xA0
#define OP_COMPRESSEDPART_I64       0xA1
#define OP_SENDINGPART_I64          0xA2
#define OP_REQUESTPARTS_I64         0xA3
#define OP_MULTIPACKET_EXT          0xA4    // *DEPRECATED*
#define OP_CHATCAPTCHAREQ           0xA5
#define OP_CHATCAPTCHARES           0xA6
#define OP_FWCHECKUDPREQ            0xA7
#define OP_KAD_FWTCPCHECK_ACK       0xA8
#define OP_MULTIPACKET_EXT2         0xA9
#define OP_MULTIPACKETANSWER_EXT2   0xB0
#define OP_HASHSETREQUEST2          0xB1
#define OP_HASHSETANSWER2           0xB2

// Extended protocol: Client <-> Client UDP opcodes
#define OP_REASKFILEPING            0x90
#define OP_REASKACK                 0x91
#define OP_FILENOTFOUND             0x92
#define OP_QUEUEFULL                0x93
#define OP_REASKCALLBACKUDP         0x94
#define OP_DIRECTCALLBACKREQ        0x95
#define OP_PORTTEST                 0xFE

// ---------------------------------------------------------------------------
// Server.met tags
// ---------------------------------------------------------------------------

#define ST_SERVERNAME               0x01
#define ST_DESCRIPTION              0x0B
#define ST_PING                     0x0C
#define ST_FAIL                     0x0D
#define ST_PREFERENCE               0x0E
#define ST_PORT                     0x0F
#define ST_IP                       0x10
#define ST_DYNIP                    0x85
#define ST_MAXUSERS                 0x87
#define ST_SOFTFILES                0x88
#define ST_HARDFILES                0x89
#define ST_LASTPING                 0x90
#define ST_VERSION                  0x91
#define ST_UDPFLAGS                 0x92
#define ST_AUXPORTSLIST             0x93
#define ST_LOWIDUSERS               0x94
#define ST_UDPKEY                   0x95
#define ST_UDPKEYIP                 0x96
#define ST_TCPPORTOBFUSCATION       0x97
#define ST_UDPPORTOBFUSCATION       0x98

// ---------------------------------------------------------------------------
// File tags
// ---------------------------------------------------------------------------

#define FT_FILENAME                 0x01
#define TAG_FILENAME                "\x01"
#define FT_FILESIZE                 0x02
#define TAG_FILESIZE                "\x02"
#define FT_FILESIZE_HI              0x3A
#define TAG_FILESIZE_HI             "\x3A"
#define FT_FILETYPE                 0x03
#define TAG_FILETYPE                "\x03"
#define FT_FILEFORMAT               0x04
#define TAG_FILEFORMAT              "\x04"
#define FT_LASTSEENCOMPLETE         0x05
#define FT_TRANSFERRED              0x08
#define FT_GAPSTART                 0x09
#define TAG_GAPSTART                "\x09"
#define FT_GAPEND                   0x0A
#define TAG_GAPEND                  "\x0A"
#define FT_DESCRIPTION              0x0B
#define TAG_DESCRIPTION             "\x0B"
#define TAG_PING                    "\x0C"
#define TAG_FAIL                    "\x0D"
#define TAG_PREFERENCE              "\x0E"
#define TAG_PORT                    "\x0F"
#define TAG_IP_ADDRESS              "\x10"
#define TAG_VERSION                 "\x11"
#define FT_PARTFILENAME             0x12
#define TAG_PARTFILENAME            "\x12"
#define FT_STATUS                   0x14
#define TAG_STATUS                  "\x14"
#define FT_SOURCES                  0x15
#define TAG_SOURCES                 "\x15"
#define FT_PERMISSIONS              0x16
#define TAG_PERMISSIONS             "\x16"
#define FT_DLPRIORITY               0x18
#define FT_ULPRIORITY               0x19
#define FT_COMPRESSION              0x1A
#define FT_CORRUPTED                0x1B
#define FT_KADLASTPUBLISHKEY        0x20
#define FT_KADLASTPUBLISHSRC        0x21
#define FT_FLAGS                    0x22
#define FT_DL_ACTIVE_TIME           0x23
#define FT_CORRUPTEDPARTS           0x24
#define FT_DL_PREVIEW               0x25
#define FT_KADLASTPUBLISHNOTES      0x26
#define FT_AICH_HASH                0x27
#define FT_FILEHASH                 0x28
#define FT_COMPLETE_SOURCES         0x30
#define TAG_COMPLETE_SOURCES        "\x30"
#define FT_COLLECTIONAUTHOR         0x31
#define FT_COLLECTIONAUTHORKEY      0x32
#define FT_PUBLISHINFO              0x33
#define TAG_PUBLISHINFO             "\x33"
#define FT_LASTSHARED               0x34
#define FT_AICHHASHSET              0x35
#define TAG_KADAICHHASHPUB          "\x36"
#define TAG_KADAICHHASHRESULT       "\x37"
#define FT_FOLDERNAME               0x38

// Statistic tags
#define FT_ATTRANSFERRED            0x50
#define FT_ATREQUESTED              0x51
#define FT_ATACCEPTED               0x52
#define FT_CATEGORY                 0x53
#define FT_ATTRANSFERREDHI          0x54
#define FT_MAXSOURCES               0x55

// Media tags
#define FT_MEDIA_ARTIST             0xD0
#define TAG_MEDIA_ARTIST            "\xD0"
#define FT_MEDIA_ALBUM              0xD1
#define TAG_MEDIA_ALBUM             "\xD1"
#define FT_MEDIA_TITLE              0xD2
#define TAG_MEDIA_TITLE             "\xD2"
#define FT_MEDIA_LENGTH             0xD3
#define TAG_MEDIA_LENGTH            "\xD3"
#define FT_MEDIA_BITRATE            0xD4
#define TAG_MEDIA_BITRATE           "\xD4"
#define FT_MEDIA_CODEC              0xD5
#define TAG_MEDIA_CODEC             "\xD5"

// Misc tags
#define TAG_KADMISCOPTIONS          "\xF2"
#define TAG_ENCRYPTION              "\xF3"
#define TAG_USER_COUNT              "\xF4"
#define TAG_FILE_COUNT              "\xF5"
#define FT_FILECOMMENT              0xF6
#define TAG_FILECOMMENT             "\xF6"
#define FT_FILERATING               0xF7
#define TAG_FILERATING              "\xF7"
#define TAG_BUDDYHASH               "\xF8"
#define TAG_CLIENTLOWID             "\xF9"
#define TAG_SERVERPORT              "\xFA"
#define TAG_SERVERIP                "\xFB"
#define TAG_SOURCEUPORT             "\xFC"
#define TAG_SOURCEPORT              "\xFD"
#define TAG_SOURCEIP                "\xFE"
#define TAG_SOURCETYPE              "\xFF"

// ---------------------------------------------------------------------------
// Tag types
// ---------------------------------------------------------------------------

#define TAGTYPE_NONE                0x00
#define TAGTYPE_HASH                0x01
#define TAGTYPE_STRING              0x02
#define TAGTYPE_UINT32              0x03
#define TAGTYPE_FLOAT32             0x04
#define TAGTYPE_BOOL                0x05
#define TAGTYPE_BOOLARRAY           0x06
#define TAGTYPE_BLOB                0x07
#define TAGTYPE_UINT16              0x08
#define TAGTYPE_UINT8               0x09
#define TAGTYPE_BSOB                0x0A
#define TAGTYPE_UINT64              0x0B
#define TAGTYPE_UINT                0xFE // general uint: 8, 16, 32, 64 bits

#define TAGTYPE_STR1                0x11
#define TAGTYPE_STR2                0x12
#define TAGTYPE_STR3                0x13
#define TAGTYPE_STR4                0x14
#define TAGTYPE_STR5                0x15
#define TAGTYPE_STR6                0x16
#define TAGTYPE_STR7                0x17
#define TAGTYPE_STR8                0x18
#define TAGTYPE_STR9                0x19
#define TAGTYPE_STR10               0x1A
#define TAGTYPE_STR11               0x1B
#define TAGTYPE_STR12               0x1C
#define TAGTYPE_STR13               0x1D
#define TAGTYPE_STR14               0x1E
#define TAGTYPE_STR15               0x1F
#define TAGTYPE_STR16               0x20
#define TAGTYPE_STR17               0x21
#define TAGTYPE_STR18               0x22
#define TAGTYPE_STR19               0x23
#define TAGTYPE_STR20               0x24
#define TAGTYPE_STR21               0x25
#define TAGTYPE_STR22               0x26

// ---------------------------------------------------------------------------
// ED2K file type strings
// ---------------------------------------------------------------------------

#define ED2KFTSTR_ANY               ""
#define ED2KFTSTR_AUDIO             "Audio"
#define ED2KFTSTR_VIDEO             "Video"
#define ED2KFTSTR_IMAGE             "Image"
#define ED2KFTSTR_DOCUMENT          "Doc"
#define ED2KFTSTR_PROGRAM           "Pro"
#define ED2KFTSTR_ARCHIVE           "Arc"
#define ED2KFTSTR_CDIMAGE           "Iso"
#define ED2KFTSTR_EMULECOLLECTION   "EmuleCollection"

// Additional media meta data tags from eDonkeyHybrid
#define FT_ED2K_MEDIA_ARTIST        "Artist"
#define FT_ED2K_MEDIA_ALBUM         "Album"
#define FT_ED2K_MEDIA_TITLE         "Title"
#define FT_ED2K_MEDIA_LENGTH        "length"
#define FT_ED2K_MEDIA_BITRATE       "bitrate"
#define FT_ED2K_MEDIA_CODEC         "codec"
#define TAG_NSENT                   "# Sent"
#define TAG_ONIP                    "ip"
#define TAG_ONPORT                  "port"

// ---------------------------------------------------------------------------
// ED2K search expression comparison operators
// ---------------------------------------------------------------------------

#define ED2K_SEARCH_OP_EQUAL            0
#define ED2K_SEARCH_OP_GREATER          1
#define ED2K_SEARCH_OP_LESS             2
#define ED2K_SEARCH_OP_GREATER_EQUAL    3
#define ED2K_SEARCH_OP_LESS_EQUAL       4
#define ED2K_SEARCH_OP_NOTEQUAL         5

// ---------------------------------------------------------------------------
// Client info tags (CT_*)
// ---------------------------------------------------------------------------

#define CT_NAME                     0x01
#define CT_PORT                     0x0f
#define CT_VERSION                  0x11
#define CT_SERVER_FLAGS             0x20
#define CT_MOD_VERSION              0x55
#define CT_EMULECOMPAT_OPTIONS1     0xef
#define CT_EMULE_RESERVED1          0xf0
#define CT_EMULE_RESERVED2          0xf1
#define CT_EMULE_RESERVED3          0xf2
#define CT_EMULE_RESERVED4          0xf3
#define CT_EMULE_RESERVED5          0xf4
#define CT_EMULE_RESERVED6          0xf5
#define CT_EMULE_RESERVED7          0xf6
#define CT_EMULE_RESERVED8          0xf7
#define CT_EMULE_RESERVED9          0xf8
#define CT_EMULE_UDPPORTS           0xf9
#define CT_EMULE_MISCOPTIONS1       0xfa
#define CT_EMULE_VERSION            0xfb
#define CT_EMULE_BUDDYIP            0xfc
#define CT_EMULE_BUDDYUDP           0xfd
#define CT_EMULE_MISCOPTIONS2       0xfe
#define CT_EMULE_RESERVED13         0xff
#define CT_SERVER_UDPSEARCH_FLAGS   0x0E

// ---------------------------------------------------------------------------
// Server capability flags (values for CT_SERVER_FLAGS)
// ---------------------------------------------------------------------------

#define SRVCAP_ZLIB                 0x0001
#define SRVCAP_IP_IN_LOGIN          0x0002
#define SRVCAP_AUXPORT              0x0004
#define SRVCAP_NEWTAGS              0x0008
#define SRVCAP_UNICODE              0x0010
#define SRVCAP_LARGEFILES           0x0100
#define SRVCAP_SUPPORTCRYPT         0x0200
#define SRVCAP_REQUESTCRYPT         0x0400
#define SRVCAP_REQUIRECRYPT         0x0800

// Values for CT_SERVER_UDPSEARCH_FLAGS
#define SRVCAP_UDP_NEWTAGS_LARGEFILES   0x01

// ---------------------------------------------------------------------------
// eMule tag names (ET_*)
// ---------------------------------------------------------------------------

#define ET_COMPRESSION              0x20
#define ET_UDPPORT                  0x21
#define ET_UDPVER                   0x22
#define ET_SOURCEEXCHANGE           0x23
#define ET_COMMENTS                 0x24
#define ET_EXTENDEDREQUEST          0x25
#define ET_COMPATIBLECLIENT         0x26
#define ET_FEATURES                 0x27
#define ET_MOD_VERSION              CT_MOD_VERSION

// ---------------------------------------------------------------------------
// PeerCache (defunct)
// ---------------------------------------------------------------------------

#define PCPCK_VERSION               0x01
#define PCOP_NONE                   0x00
#define PCOP_REQ                    0x01
#define PCOP_RES                    0x02
#define PCOP_ACK                    0x03
#define PCTAG_CACHEIP               0x01
#define PCTAG_CACHEPORT             0x02
#define PCTAG_PUBLICIP              0x03
#define PCTAG_PUBLICPORT            0x04
#define PCTAG_PUSHID                0x05
#define PCTAG_FILEID                0x06

// ---------------------------------------------------------------------------
// Kademlia opcodes (UDP)
// ---------------------------------------------------------------------------

#define KADEMLIA_BOOTSTRAP_REQ_DEPRECATED       0x00
#define KADEMLIA2_BOOTSTRAP_REQ                 0x01

#define KADEMLIA_BOOTSTRAP_RES_DEPRECATED       0x08
#define KADEMLIA2_BOOTSTRAP_RES                 0x09

#define KADEMLIA_HELLO_REQ_DEPRECATED           0x10
#define KADEMLIA2_HELLO_REQ                     0x11

#define KADEMLIA_HELLO_RES_DEPRECATED           0x18
#define KADEMLIA2_HELLO_RES                     0x19

#define KADEMLIA_REQ_DEPRECATED                 0x20
#define KADEMLIA2_REQ                           0x21

#define KADEMLIA2_HELLO_RES_ACK                 0x22

#define KADEMLIA_RES_DEPRECATED                 0x28
#define KADEMLIA2_RES                           0x29

#define KADEMLIA_SEARCH_REQ                     0x30
#define KADEMLIA_SEARCH_NOTES_REQ               0x32
#define KADEMLIA2_SEARCH_KEY_REQ                0x33
#define KADEMLIA2_SEARCH_SOURCE_REQ             0x34
#define KADEMLIA2_SEARCH_NOTES_REQ              0x35

#define KADEMLIA_SEARCH_RES                     0x38
#define KADEMLIA_SEARCH_NOTES_RES               0x3A
#define KADEMLIA2_SEARCH_RES                    0x3B

#define KADEMLIA_PUBLISH_REQ                    0x40
#define KADEMLIA_PUBLISH_NOTES_REQ_DEPRECATED   0x42
#define KADEMLIA2_PUBLISH_KEY_REQ               0x43
#define KADEMLIA2_PUBLISH_SOURCE_REQ            0x44
#define KADEMLIA2_PUBLISH_NOTES_REQ             0x45

#define KADEMLIA_PUBLISH_RES                    0x48
#define KADEMLIA_PUBLISH_NOTES_RES_DEPRECATED   0x4A
#define KADEMLIA2_PUBLISH_RES                   0x4B
#define KADEMLIA2_PUBLISH_RES_ACK               0x4C

#define KADEMLIA_FIREWALLED_REQ                 0x50
#define KADEMLIA_FINDBUDDY_REQ                  0x51
#define KADEMLIA_CALLBACK_REQ                   0x52
#define KADEMLIA_FIREWALLED2_REQ                0x53

#define KADEMLIA_FIREWALLED_RES                 0x58
#define KADEMLIA_FIREWALLED_ACK_RES             0x59
#define KADEMLIA_FINDBUDDY_RES                  0x5A

#define KADEMLIA2_PING                          0x60
#define KADEMLIA2_PONG                          0x61

#define KADEMLIA2_FIREWALLUDP                   0x62

// Kademlia parameters
#define KADEMLIA_FIND_VALUE                     0x02
#define KADEMLIA_STORE                          0x04
#define KADEMLIA_FIND_NODE                      0x0B
#define KADEMLIA_FIND_VALUE_MORE                KADEMLIA_FIND_NODE

// ---------------------------------------------------------------------------
// Search spam tags (searchspam.met)
// ---------------------------------------------------------------------------

#define SP_FILEFULLNAME             0x01u
#define SP_FILEHASHSPAM             0x02u
#define SP_FILEHASHNOSPAM           0x03u
#define SP_FILESOURCEIP             0x04u
#define SP_FILESERVERIP             0x05u
#define SP_FILESIMILARNAME          0x06u
#define SP_FILESIZE                 0x07u
#define SP_UDPSERVERSPAMRATIO       0x08u
