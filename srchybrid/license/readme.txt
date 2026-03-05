eMule Copyright (C)2002-2024 Merkur (devteam at emule-project dot net)


This program is free software; you can redistribute it and/or modify it under 
the terms of the GNU General Public License as published by the Free Software 
Foundation; either version 2 of the License, or (at your option) any later 
version.

This program is distributed in the hope that it will be useful, but WITHOUT 
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS 
FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more 
details.

You should have received a copy of the GNU General Public License along with 
this program; if not, write to the Free Software Foundation, Inc., 675 Mass 
Ave, Cambridge, MA 02139, USA.


-------------------------------------------------------------------------------


Welcome to eMule, a filesharing client based on the eDonkey2000(C) network.


Visit us at
 https://www.emule-project.net
and
 https://sourceforge.net/projects/emule
or
 the IRC chatrooms on irc.mindforge.org
 #emule-english
  ... and channels in other languages



Visit our forum for bug reports, feature requests, development or general 
dicussion.


If you have questions or serious problems, please read the FAQ first :) It can 
be found at www.emule-project.net. A small collection of questions is also 
discussed later in this document.


If you didn't find an answer, SEARCH the forum for a topic related to your 
problem, DO NOT open a new topic at once, most likely someone else had the 
same problem before.

The official Forum is also at https://www.emule-project.net Please use only 
English there, except for the language-specific sections. (PLEASE do not 
report bugs that are already posted by someone else, and keep the "Bug 
Reports", "Feature Requests" and "Development" sections clean and shiny)




INSTALLATION:
-------------

-Unzip eMule to a directory of your choice, or start the installer if you 
 downloaded the .exe installer version.
 
-You can move your "Temp" and "Incoming" folders from eDonkey (or a previous 
 version of eMule) to the new directory now, in order to continue your partial 
 downloads. If you don't want to move your folders, you can set the "temp" and 
 "incoming" path in the eMule preferences and restart it to get the same 
 result.

-Updating from an earlier version of eMule: The best way to do this is simply 
 to download the .zip file (not the installer), and unzip the new emule.exe to 
 your old emule directory, overwriting the previous one.
 To update Web Interface it might be required also to unpack "webserver"
 directory and template file eMule.tmpl



CONFIGURATION:
--------------

-Go to the "Preferences" tab

-Enter a nice nickname ;)

-Enter the "Download Capacity" and "Upload Capacity" according to your 
 internet connection. All values in eMule are kiloBytes (KB), your Internet 
 Service Provider's (ISP) numbers are most likely kiloBits (kB). 8 Bits make up 
 1 Byte, so when your Internet Connection is 768kB Downstrean and 128kB 
 Upstrean (like German Telekom DSL), your correct values are:

 Downstrean: 768kB / 8 = 96KB, you enter 96 as "Download Capacity"
 Upstream: 128kB / 8 = 16 KB, you enter 16 as "Upload Capacity"

 The "capacity" values are used for the statistics display only. Nevertheless, 
 you need to know them to determine the following down/upload limits:


-Enter "Download Limit" and "Upload Limit"  (IMPORTANT!)
 
 Download Limit: leave this at 0 (should eMule become too fast and you are 
 unable to surf the Internet or whatever, reduce it to 80-90% of "Download 
 Capacity")
 
 Upload Limit: set this to ~80% of your "Upload Capacity" (so when your Upload 
 Capacity is 16, set Upload Limit to 12 or 13)

 Setting Upload Limit to a value < 10 will automatically reduce your Download 
 Limit, so upload as fast as you can.


NOTE: 56k Modem users: eMule only accepts integral values for these settings 
at the moment, you can't enter 2.6 or whatever your sweet-spot setting is, 
yet. Sorry :) Maybe later..


-"Maximum Connections": depends on your operating system. As a general rule...

  -Windows 98/ME (and 56k Modem/ISDN) users enter 50 here
  -Windows 2000/XP users should set this according to their 
   Internet Connection. 250 is a good value for 128k upstream connection, 
   for example. DO NOT set this too high. It will kill your upload and with 
   that, your download.

-"Maximum Sources per File": decide for yourself how many you want :) when you 
 set this too high, your computer might slow down drastically or even crash. 
 500-1000 are good values for people with DSL/cable connection.

-Choose the directories you want to share with other users. DO NOT SHARE YOUR 
 COMPLETE HARDDISK! Put the stuff you want to share in a separate Folder. If 
 you share more than ~500 files, you should reconsider that...

-The other options are pretty self-explaining. If you dunno what it does, 
don't touch it.



FAQ: (for more, see https://www.emule-project.net/faq/ )
----

--"Will I lose my credits when switching to a new version of eMule?"

 Not when move your old preferences.dat (your user ID) and clients.met (other 
 people's credits) files to the directory you installed the new version in. 
 The best way to update is just to replace your old emule.exe with the new one 
 from the .zip download.



--"Why is eMule so slow? My brother/friend/whatever is downloading at 100K constantly"

 When you did setup eMule properly, it's all about the availibility of the 
 files you are downloading, a bit of luck and a lot of patience ;)



--"Where can I get a new serverlist?"

 There are several lists availible. Some that I know of are:
 - https://edk.peerates.net
 - https://emule-security.org
 Note that those sites are not related to the eMule project, we are not 
 responsible for their content.



--"What is the addresses.dat file for?"

 You can enter a serverlist URL in that file. eMule will then get the 
 serverlist from that URL at startup (when the option "Auto-update serverlist 
 at startup" is activated)



--"What is the staticservers.dat file good for?"

 You can enter your favorite servers here to have them permanently in your 
 serverlist with high priority. You can enter the static IP of the server, or 
 an adress like goodserver.dyndns.net. You can also add static servers to this 
 file via the server tab in eMule (right-click -> add to static serverlist)


--"Why do I always have a low ID (means: firewalled) ??? What can I do against that?"

 Look here: https://www.emule-project.net/faq/ports.htm



--"How do I know whether my ID is high or low?"

 Look at the arrow in the bottom right corner, next to the server name you are 
 connected to. When it's green, your ID is high. When it's orange, your ID is 
 low.
 


--"What does high and low ID mean anyway?"

 When your ID is high (green arrow), everything is fine :) When it's low 
 (yellow arrow), you are behind a firewall or router, and other clients can't 
 connect to you directly (which is a bad thing). Please read the FAQ or search 
 the forums on how to configure your firewall/router for eMule.

 NOTE: you can also get a low ID when the server you connected to is too busy 
 to answer properly, or simply badly configured. When you are sure your 
 settings are ok and you SHOULD have a high ID, connect to antoher server.



--"What is the difference between up/down CAPACITY and LIMIT?"

 The CAPACITY is used only by the statistcs tab to determine the vertical 
 limits of the diagram. The LIMITS set the actual network traffic limits (see 
 configuration notes).



--"I'd like to search for specific file types, what filter stands for which files?"

 File Type        Extensions found
 --------------------------------------------------------------------------------
 Audio          .mp3 .mp2 .mpc .wav .ogg .aac .mp4 .ape .au .wma .flac
 Video          .avi .mpg .mpeg .ram .rm .vob .divx .mov .ogm .vivo
 Program        .exe .com
 Archive        .zip .rar .ace
 CD-Image       .bin .cue .iso .nrg .ccd .sub .img .bwt .bwi .bwa .bws .mdf .mds
 Picture        .jpg .jpeg .bmp .gif .tif .png



--"What are all those fancy colours in the download progress bar about?"

 Each download in the the Transfer tab has a coloured bar to show current file 
 availability and progress.

 -Black shows the parts of the file you already have
 -Red indicates a part missing in all known sources
 -Different shades of blue represent the availability of this part in the
  sources. The darker the blue is the higher the availability
 -Yellow denotes a part being downloaded
 -The green bar on top shows the total download progress of this file

 If you expand the download you see its sources with the corresponding bar. 
 Here the colours have a slightly different meaning:

 -Black shows parts you are still missing
 -Silver stands for parts this source is also missing
 -Green indicates parts you already have
 -Yellow denotes a part being uploaded to you

 Learning how the progress bar works will greatly help your understanding of 
 the eDonkey2000 network.



--"What do the "QR: xxxx" numbers mean that I see when I look at my sources?"

 QR stands for "Queue Rank" and it is your current position in this source's 
 queue.

 Obviously, a lower value is better :) If the source is an eMule client and 
 there is no QR number, it's likely that it's queue is full and cannot accept 
 more clients.




COMPILING THE SOURCECODE:
-------------------------

The sourcecode of eMule is availible as separate download at:
 - official version
 https://sourceforge.net/projects/emule
 https://github.com/SomeSupport/eMule
 - community version
 https://github.com/irwir/eMule
You need Microsoft(C) Visual Studio to build; the required version depends on eMule's version.
The latest community version code was compiled with Visual Studio 2022.

-Unzip the sources (with subdirs) into a new folder

Since eMule v0.29b the separate download and compilation of additional 
libraries is required. These libs are 3rd party and therefore usually are not 
within our distribution. You need the following libs:

*) Crypto++ 8.9
        https://www.cryptopp.com

*) CxImage 7.02
        https://sourceforge.net/projects/cximage/

*) id3lib v3.8.3
        https://sourceforge.net/projects/id3lib/

*) libpng 1.6.43
        http://www.libpng.org/pub/png/libpng.html

*) Mbed TLS 3.6.0
        https://www.trustedfirmware.org/projects/mbed-tls/

*) miniupnpc 2.2.8
        http://miniupnp.free.fr

*) ResizableLib 1.5.3
        https://github.com/ppescher/resizablelib

*) zlib v1.3.1
        http://zlib.net

Download and save their sourcecode one level above the eMule source code 
folder and compile them (check the eMule-project file to learn about the 
required folder names, or adapt them to your needs!).

-IMPORTANT:

 If you make modifications to eMule and you distribute the compiled version of 
 your build, be sure to obey the GPL license. You have to include the 
 sourcecode, or make it availible for download together with the binary 
 version.




MISC. STUFF:
------------

The eMule staff would like to thank the following (no particular order)

-Jed "swamp" McCaleb and MetaMachine for bringing the eDonkey network to life
-DrSiRiUs for the awesome artwork (splashscreen and icons)
-OCBmaurice & Stillman for the serverlists and more (www.thedonkeynetwork.com)

-many people who sent us code parts, useful bug reports and useful suggestions :)




STAFF:
------

Who are the guys that sacrifice so much time to bring you this wonderful 
program?

-John aka. Unknown1 - Developer
-Ornis - Developer
-bluecow - Developer
-zz - Developer
-Monk - Testers

-Merkur (merkur-@users.sourceforge.net) - Master of the Mule - retired, we miss you :)
-Tecxx - Developer (retired)
-Pach2 - Developer (retired)
-Juanjo - Developer (retired)
-Dirus - Developer (retired)
-Barry - Developer (retired)
-Mr. Ozon, Sony, Myxin - Tester (retired)



LEGAL:
------

eMule Copyright (C)2002-2024 Merkur (devteam at emule-project dot net)

eDonkey2000 (C)Jed McCaleb, MetaMachine (www.eDonkey2000.com)

Windows(TM), Windows 95(TM), Windows 98(TM), Windows ME(TM), Windows NT(TM), 
Windows 2000(TM), Windows XP(TM), Windows Vista(TM), Windows 7(TM), Windows 8(TM),
Windows 8.1(TM) and Windows 10(TM) are Copyright (C) Microsoft Corporation. 
All rights reserved.


Goodbye and happy sharing ;)
