/*
   Unix SMB/CIFS implementation.
   SMB parameters and setup
   Copyright (C) Andrew Tridgell 1992-1998
   Copyright (C) John H Terpstra 1996-1998
   Copyright (C) Luke Kenneth Casson Leighton 1996-1998
   Copyright (C) Paul Ashton 1998

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef _MAC_EXTENSIONS_H
#define _MAC_EXTENSIONS_H

/* Folder that holds the stream info */
#define STREAM_FOLDER 					".streams"
#define STREAM_FOLDER_SLASH 			".streams/"

/* Common Streams Names*/
#define DefaultStreamTestLen	6
#define DefaultStreamTest		":$DATA"
#define AFPDATA_STREAM 			"::$DATA"

#define AFPINFO_STREAM_NAME		":AFP_AfpInfo"
#define AFPRESOURCE_STREAM_NAME		":AFP_Resource"
#define AFPCOMMENTS_STREAM_NAME		":Comments"
#define AFPDESKTOP_STREAM_NAME 		":AFP_DeskTop"
#define AFPIDINDEX_STREAM_NAME 		":AFP_IdIndex"

#define AFPINFO_STREAM 			AFPINFO_STREAM_NAME ":$DATA"
#define AFPRESOURCE_STREAM 		AFPRESOURCE_STREAM_NAME ":$DATA"
#define AFPCOMMENTS_STREAM 		AFPCOMMENTS_STREAM_NAME ":$DATA"
#define AFPDESKTOP_STREAM 		AFPDESKTOP_STREAM_NAME ":$DATA"
#define AFPIDINDEX_STREAM 		AFPIDINDEX_STREAM_NAME ":$DATA"

/*
** NT's AFP_AfpInfo stream structure
*/
#define AFP_INFO_SIZE		0x3c
#define AFP_Signature		0x41465000
#define AFP_Version			0x00000100
#define AFP_BackupTime		0x80000000
#define AFP_FinderSize		32

#define AFP_OFF_FinderInfo	16

/*
** Original AFP_AfpInfo stream used by NT
** We needed a way to store the create date so SAMBA
** AFP_AfpInfo adds for bytes to this structrure
** and call's it _SambaAfpInfo
*/
typedef struct _AfpInfo
{
	 uint32_t      	afpi_Signature;   		/* Must be *(PDWORD)"AFP" */
	 uint32_t      	afpi_Version;     		/* Must be 0x00010000 */
	 uint32_t      	afpi_Reserved1;
	 uint32_t      	afpi_BackupTime;  		/* Backup time for the file/dir */
	 unsigned char 	afpi_FinderInfo[AFP_FinderSize];  	/* Finder Info (32 bytes) */
	 unsigned char 	afpi_ProDosInfo[6];  	/* ProDos Info (6 bytes) # */
	 unsigned char 	afpi_Reserved2[6];
} AfpInfo;

typedef struct _SambaAfpInfo
{
	 AfpInfo       	afp;
	 unsigned long 	createtime;
} SambaAfpInfo;

/*
** On SAMBA this structure is followed by 4 bytes that store the create
** date of the file or folder associated with it.
*/

/*
** These extensions are only supported with the NT LM 0.12 Dialect. These extensions
** will be process on a share by share bases.
*/

/*
** Trans2_Query_FS_Information Call is used by the MacCIFS extensions for three reasons.
** First to see if the remote server share supports the basic Macintosh CIFS extensions.
** Second to return some basic need information about the share to the Macintosh.
** Third to see if this share support any other Macintosh extensions.
**
** We will be using information levels that are between 0x300 and 0x399 for all Macintosh
** extensions calls. The first of these will be the SMB_MAC_QUERY_FS_INFO level which
** will allow the server to return the MacQueryFSInfo structure. All fields are Little
** Endian unless otherwise specified.
*/
#define SMB_MAC_QUERY_FS_INFO 0x301



/*
** The server will return folder access control in the Trans2_Find_First2
** and Trans2_Find_Next2 message described later in this document.
*/
#define SUPPORT_MAC_ACCESS_CNTRL	0x0010
/*
** The server supports setting/getting comments using the mechanism in this
** document instead of using the NTFS format described in the Introduction.
*/
#define SUPPORT_MAC_GETSETCOMMENTS	0x0020
/*
** The Server supports setting and getting Macintosh desktop database information
** using the mechanism in this document.
*/
#define SUPPORT_MAC_DESKTOPDB_CALLS	0x0040
/*
** The server will return a unique id for files and directories in the
** Trans2_Find_First2 and Trans2_Find_Next2 message described later in this document.
*/
#define SUPPORT_MAC_UNIQUE_IDS		0x0080
/*
** The server will return this flag telling the client that the server does
** not support streams or the Macintosh extensions. The rest of this message
** will be ignored by the client.
*/
#define NO_STREAMS_OR_MAC_SUPPORT	0x0100

/*
** We will be adding a new info level to the Trans2_Find_First2 and Trans2_Find_Next2.
** This info level will be SMB_MAC_FIND_BOTH_HFS_INFO and will support the server
** return additional information need by the Macintosh. All fields are Little
** Endian unless other wise specified.
*/

#define SMB_MAC_FIND_BOTH_HFS_INFO	  0x302

enum {
	ownerRead	= 0x0400,
	ownerWrite	= 0x0200,
	ownerSearch	= 0x0100,
	groupRead	= 0x0040,
	groupWrite	= 0x0020,
	groupSearch	= 0x0010,
	otherRead	= 0x0004,
	otherWrite	= 0x0002,
	otherSearch	= 0x0001,
	Owner		= 0x0800
};


/*
** We will be adding a new info level to the Trans2_Set_Path_Information.
** This info level will be SMB_MAC_SET_FINDER_INFO and will support the client
** setting information on the server need by the Macintosh. All fields are Little
** Endian unless other wise specified.
*/

#define SMB_MAC_SET_FINDER_INFO	  0x303

enum {
	SetCreateDate	= 0x01,			/* If this is set then set the create date of the file/folder */
	SetModDate		= 0x02,			/* If this is set then set the modify date of the file/folder */
	SetFLAttrib		= 0x04,			/* If this is set then set the Macintosh lock bit of the file/folder */
	FndrInfo1		= 0x08,			/* If this is set then set the first 16 bytes of finder info */
	FndrInfo2		= 0x10,			/* If this is set then set the second 16 bytes of finder info */
	SetHidden		= 0x20			/* We are either setting or unsetting the hidden bit */
};


/*
** We will be adding some new info level to the Trans2_Set_Path_Information and Trans2_Query_Path_Information.
** These info levels will allow the client to add, get, and remove desktop information from the
** server. How the server stores this information is up to them.
*/

/*
** We need to be able to store an application name and its creator in a database. We send a
** Trans2_Set_Path_Information call with the full path of the application in the path field.
** We will send an info level that represents adding an application name and creator to the database.
** We will pass the File Creator in the data message.
**
** The server should just respond  with no error or an error.
*/
#define SMB_MAC_DT_ADD_APPL	  0x304

/*
** We need to be able to remove an application name and its creator from a database. We send a
** Trans2_Set_Path_Information call with the full path of the application in the path field.
** We will send an info level that represents removing an application name and creator from the database.
** We will pass the File Creator in the data message.
**
** The server should just respond  with no error or an error.
*/
#define SMB_MAC_DT_REMOVE_APPL	  0x305


/*
** We need to be able to get an application name and its creator from a database. We send a
** Trans2_Query_Path_Information call in which the name field is just ignore.
** We will send an info level that represents getting an application name with a structure that
** contains the File Creator and index. Were index has the following meaning.
**		Index = 0; Get the application path from the database with the most current date.
**		Index > 0; Use the index to find the application path from the database.
**				e.g. 	index of 5 means get the fifth entry of this application name in the database.
**						if not entry return an error.
**
** The server returns with a structure that contains the full path to the application and
** its creator's date.
*/
#define SMB_MAC_DT_GET_APPL	  0x306


/*
** We need to be able to get an icon from a database. We send a Trans2_Query_Path_Information call in
** which the path name is ignore. We will send an info level that represents getting an icon with a structure
** that contains the Requested size of the icon, the Icon type, File Creator, and File Type.
**
** The server returns with a structure that contains the actual size of the icon
** (must be less than requested length) and the icon bit map.
*/
#define SMB_MAC_DT_GET_ICON	  0x307


/*
** We need to be able to get an icon from a database. We send a Trans2_Query_Path_Information call in
** which the path name is ignore. We will send an info level that represents getting an icon with a structure
** that contains the index and File Creator. The index allows the client to make repeated calls to the server
** gathering all icon stored by this file creator.
**
**
** The server returns with a structure that contains the actual size of the icon
** (must be less than requested length) and the icon bit map, File Type, and Icon Type.
*/
#define SMB_MAC_DT_GET_ICON_INFO	  0x308



/*
** We need to be able to add an icon to a database. We send a Trans2_Set_Path_Information call in
** which the path name is ignore. We will send an info level that represents setting an icon with a structure
** that contains the icon data, icon size, icon type, the file type, and file creator.
**
**
** The server returns only that the call was successful or not.
*/
#define SMB_MAC_DT_ADD_ICON	  0x309

#endif /* _MAC_EXTENSIONS_H */

/* _MAC_EXTENSIONS_H */

