# Log Pseudo Indexer (logpi)

by Ron Dilley <ron.dilley@uberadmin.com>

You can find the latest information on wirespy [here](http://www.uberadmin.com/Projects/logpi/ "Log Pseudo Indexer")

## What is Log Psuedo Indexer (logpi)?

Logpi parses text log files and outputs occurences of common network
addressess including IPv4, IPv6 and MAC addresses.  These pseudo index or
metadata files can be used to accelerate searching large log data sets
for network addresses.

When I say fast:

Processing a 29gb log file on a Mac Powerbook laptop takes ~12 minutes to index and ~30 seconds to query for multiple addresses.

```sh
$ ls -la ./messages.log
-rw-r--r--@ 1 user  staff  29187706427 Mar 16 18:43 ./messages.log
```

```sh
$ time logpi -w ./messages.log.lpi ./messages.log 
Opening [./messages.log] for read
Processed 6319938 lines/min
Processed 5731022 lines/min
Processed 5577190 lines/min
Processed 5783020 lines/min
Processed 5662040 lines/min
Processed 5754075 lines/min
Processed 5652959 lines/min
Processed 5684849 lines/min
Processed 5767093 lines/min
Processed 5910607 lines/min
```

As the command above shows, the pseudo indexer is parsing and indexing ~5.7m lines per minute.

The indexing is fast, but what really makes this tool useful for information security practitioners is the speed to search for address hits after the pseudo index has been generated.

```sh
$ time spi 132.236.56.252,54.250.186.218 ./messages.log
Searching for 132.236.56.252 54.250.186.218 
Opening [./messages.log.lpi] for read
MATCH [132.236.56.252] with 2 lines
Opening [./messages.log] for read
Mar 16 00:27:07 10.143.2.123 1,2018/03/16 00:27:06,001901000402,TRAFFIC,end,1,2018/03/16 00:27:06,10.131.239.142,132.236.56.252,168.161.192.15,132.236.56.252,Allow Outbound,,,dns,vsys1,inside,outside,ethernet1/22,ethernet1/21,LA_log,2018/03/16 00:27:06,34411943,1,7001,53,38811,53,0x404019,udp,allow,211,88,123,2,2018/03/16 00:26:06,59,any,0,185835010247,0x0,10.0.0.0-10.255.255.255,US,0,1,1,aged-out,12,0,0,0,,PA1,from-policy
Mar 16 00:39:52 10.143.2.123 1,2018/03/16 00:39:52,001901000402,THREAT,url,1,2018/03/16 00:39:52,10.146.58.93,54.250.186.218,168.161.192.16,54.250.186.218,Allow Outbound,,,ssl,vsys1,inside,outside,ethernet1/22,ethernet1/21,LA_log,2018/03/16 00:39:52,34754943,1,54989,443,25285,443,0x40f000,tcp,alert,"messenger-ws.direct.ly/",(9999),unknown,informational,client-to-server,70297609432,0x0,10.0.0.0-10.255.255.255,JP,0,,0,,,0,,,,,,,,0,12,0,0,0,,PA1,

real	0m36.670s
user	0m6.904s
sys	0m14.143s
```

```sh
$ time spi -q 132.236.56.252,54.250.186.218 ./messages.log
Searching for 132.236.56.252 54.250.186.218 
Opening [./messages.log.lpi] for read
MATCH [132.236.56.252] with 2 lines

real	0m0.010s
user	0m0.002s
sys	0m0.003s
```

## Why use it?

I built this tool to solve a log searching/analysis problem that I have
suffered through while responding to many security breaches.  It always takes
far too long to answer the question "have I seen this network address". I like
to keep two years of syslog generated logs at all times so indexing everying
becomes a complex lexical and resource/IO problem.

The simple answer was to index the fields that are easy to identify in
arbitrary log data. 

If you need to find a something fast without the expense of parsing and
indexing everything, then this is the tool for you.

## Implementation

Below are the options that wirespy supports.

```
spi v0.9 [Jul 14 2025 - 13:02:25]

syntax: spi [options] searchterm[,searchterm] filename [filename ...]
 -d|--debug (0-9)       enable debugging info
 -f|--file {fname}      use search terms stored in a file
 -h|--help              this info
 -q|--quick             quick mode, report matches and counts only
 -v|--version           display version information
 -w|--write {fname}     write output to file (default: stdout)
 searchterm             a comma separated list of search terms
 filename               one or more files to process, use '-' to read from stdin
```

Running the command line tool is simple:

```sh
$ logpi /var/log/syslog
Opening [/var/log/syslog] for read
192.168.103.2,20,401:11,382:11,350:11,332:11,313:11,295:11,271:11,253:11,235:11,216:11,197:11,172:11,154:11,135:11,117:11,94:11,76:11,58:11,39:11,20:11
192.168.103.133,80,408:9,399:11,398:9,397:9,391:9,380:11,379:9,378:9,356:9,348:11,347:9,346:9,340:9,330:11,329:9,328:9,322:9,311:11,310:9,309:9,304:9,293:11,292:9,291:9,278:9,269:11,268:9,267:9,261:9,251:11,250:9,249:9,244:9,233:11,232:9,231:9,218:9,214:11,213:9,212:9,204:9,195:11,194:9,193:9,181:9,170:11,169:9,168:9,162:9,152:11,151:9,150:9,142:9,133:11,132:9,131:9,125:9,115:11,114:9,113:9,103:9,92:11,91:9,90:9,85:9,74:11,73:9,72:9,66:9,56:11,55:9,54:9,47:9,37:11,36:9,35:9,29:9,18:11,17:9,16:9
255.255.255.0,20,400:12,381:12,349:12,331:12,312:12,294:12,270:12,252:12,234:12,215:12,196:12,171:12,153:12,134:12,116:12,93:12,75:12,57:12,38:12,19:12
127.0.0.1,9,375:11,372:11,371:11,368:11,365:11,364:11,190:11,187:11,186:11
192.168.103.254,60,402:12,398:11,397:13,383:12,379:11,378:13,351:12,347:11,346:13,333:12,329:11,328:13,314:12,310:11,309:13,296:12,292:11,291:13,272:12,268:11,267:13,254:12,250:11,249:13,236:12,232:11,231:13,217:12,213:11,212:13,198:12,194:11,193:13,173:12,169:11,168:13,155:12,151:11,150:13,136:12,132:11,131:13,118:12,114:11,113:13,95:12,91:11,90:13,77:12,73:11,72:13,59:12,55:11,54:13,40:12,36:11,35:13,21:12,17:11,16:13
```

The output are text files with <CR> delimeted records.

Each record is as follows:
```
{NETWORK ADDRESS},{OCCURENCE COUNT},{LINE NUMBER}:{FIELD OFFSET},...<CR>
```

Where there will be one or more line/offset pairs.

Searching using the pseudo indexes is simplified by using searchpi (spi).

To search using a pseudo index, use the seachpi (spi) command and specify your terms 
on the command line, or in a file and use the '-f|--file' option.  The two following
examples are equivalent.

```sh
$ spi 132.236.56.252,54.250.186.218 ./messages.log
Searching for 132.236.56.252 54.250.186.218 
Opening [./messages.log.lpi] for read
MATCH [132.236.56.252] with 2 lines
Opening [./messages.log] for read
Mar 16 00:27:07 10.143.2.123 1,2018/03/16 00:27:06,001901000402,TRAFFIC,end,1,2018/03/16 00:27:06,10.131.239.142,132.236.56.252,168.161.192.15,132.236.56.252,Allow Outbound,,,dns,vsys1,inside,outside,ethernet1/22,ethernet1/21,LA_log,2018/03/16 00:27:06,34411943,1,7001,53,38811,53,0x404019,udp,allow,211,88,123,2,2018/03/16 00:26:06,59,any,0,185835010247,0x0,10.0.0.0-10.255.255.255,US,0,1,1,aged-out,12,0,0,0,,PA1,from-policy
Mar 16 00:39:52 10.143.2.123 1,2018/03/16 00:39:52,001901000402,THREAT,url,1,2018/03/16 00:39:52,10.146.58.93,54.250.186.218,168.161.192.16,54.250.186.218,Allow Outbound,,,ssl,vsys1,inside,outside,ethernet1/22,ethernet1/21,LA_log,2018/03/16 00:39:52,34754943,1,54989,443,25285,443,0x40f000,tcp,alert,"messenger-ws.direct.ly/",(9999),unknown,informational,client-to-server,70297609432,0x0,10.0.0.0-10.255.255.255,JP,0,,0,,,0,,,,,,,,0,12,0,0,0,,PA1,
```

```sh
$ cat ./searchterms.txt 
132.236.56.25
254.250.186.218
$ spi -f ./searchterms.txt ./messages.log
Searching for 132.236.56.252 54.250.186.218 
Opening [./messages.log.lpi] for read
MATCH [132.236.56.252] with 2 lines
Opening [./messages.log] for read
Mar 16 00:27:07 10.143.2.123 1,2018/03/16 00:27:06,001901000402,TRAFFIC,end,1,2018/03/16 00:27:06,10.131.239.142,132.236.56.252,168.161.192.15,132.236.56.252,Allow Outbound,,,dns,vsys1,inside,outside,ethernet1/22,ethernet1/21,LA_log,2018/03/16 00:27:06,34411943,1,7001,53,38811,53,0x404019,udp,allow,211,88,123,2,2018/03/16 00:26:06,59,any,0,185835010247,0x0,10.0.0.0-10.255.255.255,US,0,1,1,aged-out,12,0,0,0,,PA1,from-policy
Mar 16 00:39:52 10.143.2.123 1,2018/03/16 00:39:52,001901000402,THREAT,url,1,2018/03/16 00:39:52,10.146.58.93,54.250.186.218,168.161.192.16,54.250.186.218,Allow Outbound,,,ssl,vsys1,inside,outside,ethernet1/22,ethernet1/21,LA_log,2018/03/16 00:39:52,34754943,1,54989,443,25285,443,0x40f000,tcp,alert,"messenger-ws.direct.ly/",(9999),unknown,informational,client-to-server,70297609432,0x0,10.0.0.0-10.255.255.255,JP,0,,0,,,0,,,,,,,,0,12,0,0,0,,PA1,
```

The searchpi (spi) command will decompress files that are zlib compressed and have an '*.gz' extension.

## Security Implications

Assume that there are errors in the source that
would allow a specially crafted packet to allow an attacker
to exploit wsd to gain access to the computer that it is
running on!!!  The tool tries to get rid of priviledges it does
not need and can run in a chroot environment.  I recommend
that you use the chroot and uid/gid options.  They are there
to compensate for my poor programming skills.  Don't trust
this software and install and use is at your own risk.

## Bugs

I am not a programmer by any stretch of the imagination.  I
have attempted to remove the obvious bugs and other
programmer related errors but please keep in mind the first
sentence.  If you find an issue with code, please send me
an e-mail with details and I will be happy to look into
it.

Ron Dilley
ron.dilley@uberadmin.com
