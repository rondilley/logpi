# Log Pseudo Indexer (logpi)

by Ron Dilley <ron.dilley@uberadmin.com>

You can find the latest information on logpi [here](http://www.uberadmin.com/Projects/logpi/ "Log Pseudo Indexer")

## What is Log Pseudo Indexer (logpi)?

Logpi is a high-performance log analysis tool that parses text log files and extracts network addresses (IPv4, IPv6, and MAC addresses) to create pseudo-index files. These index files enable extremely fast searching of large log datasets for network addresses, making it an essential tool for security analysis and incident response.

## Performance Features

- **Parallel Processing**: Automatic multi-threaded processing for files >100MB
- **High Throughput**: Serial mode ~60M lines/minute, Parallel mode 125M+ lines/minute  
- **Optimized Architecture**: Dedicated I/O, parser, and hash management threads
- **Memory Efficient**: Streaming chunk processing with bounded memory usage
- **Cross-Platform**: Supports Linux, BSD, macOS, Solaris, AIX, and HP-UX

When I say fast - processing a 132GB log file on a Framework 13 Laptop takes ~6 minutes to index and ~3 seconds to query for multiple addresses:

```sh
$ ls -la massive_syslog.log
Permissions Size User    Date Modified Name
.rw-r--r--  132G rdilley 21 Aug 11:57   massive_syslog.log
```

As an aside, logpi generates the pseudo index quickly.

```sh
$ time ./src/logpi -w massive_syslog.log
Writing index to [massive_syslog.log.lpi]
Opening [massive_syslog.log] for read
Using parallel processing (4 threads) for large file (126330 MB)
Processed 134394672 lines/min
Processed 122107906 lines/min

real	4m9.429s
user	11m22.880s
sys	1m6.424s
```

As the command above shows, the pseudo indexer is parsing and indexing ~60M lines per minute using the advanced parallel processing architecture. For optimal performance on very large files, the indexer can achieve 125M+ lines/minute throughput.

### Parallel Processing Architecture

For files larger than 100MB, logpi automatically switches to a high-performance parallel processing mode:

- **I/O Thread**: Dedicated thread for reading file chunks and distributing work
- **Parser Threads**: Multiple worker threads (typically 4) that parse chunks and extract network addresses  
- **Hash Thread**: Dedicated thread for maintaining the address index with real-time updates
- **Lock-Free Communication**: Producer-consumer queues eliminate thread contention

This architecture eliminates the hash table performance bottlenecks found in traditional multi-threaded implementations by using a single hash table managed by one thread, rather than merging multiple hash tables at the end.

```sh
time ./src/logpi -w ~/data/*.log
Writing index to [~/data/auth.log.lpi]
Opening [~/data/auth.log] for read
Writing index to [~/data/authpriv.log.lpi]
Opening [~/data/authpriv.log] for read
Writing index to [~/data/cron.log.lpi]
Opening [~/data/cron.log] for read
Writing index to [~/data/daemon.log.lpi]
Opening [~/data/daemon.log] for read
Writing index to [/home/rdilley/git/AI-Tools/twt_data/kern.log.lpi]
Opening [/home/rdilley/git/AI-Tools/twt_data/kern.log] for read
Writing index to [/home/rdilley/git/AI-Tools/twt_data/local5.log.lpi]
Opening [/home/rdilley/git/AI-Tools/twt_data/local5.log] for read
Writing index to [/home/rdilley/git/AI-Tools/twt_data/local7.log.lpi]
Opening [/home/rdilley/git/AI-Tools/twt_data/local7.log] for read
Writing index to [/home/rdilley/git/AI-Tools/twt_data/mail.log.lpi]
Opening [/home/rdilley/git/AI-Tools/twt_data/mail.log] for read
Writing index to [/home/rdilley/git/AI-Tools/twt_data/syslog.log.lpi]
Opening [/home/rdilley/git/AI-Tools/twt_data/syslog.log] for read
Writing index to [/home/rdilley/git/AI-Tools/twt_data/user.log.lpi]
Opening [/home/rdilley/git/AI-Tools/twt_data/user.log] for read

real	0m2.716s
user	0m2.466s
sys	0m0.245s
```

The indexing is fast, but what really makes this tool useful for information security practitioners is the speed to search for address hits after the pseudo index has been generated.  

```sh
$ time ./src/spi 91.238.181.91,23.248.217.56 ~/data/*.log
Searching for 23.248.217.56 91.238.181.91
MATCH [91.238.181.91] with 6 lines
Opening [~/data/daemon.log] for read
Jun  9 23:59:50 192.168.10.21 spamd[27698]: 91.238.181.91: connected (1/0)
Jun  9 23:59:50 192.168.10.21 spamd[27698]: 91.238.181.91: connected (2/0)
Jun  9 23:59:52 192.168.10.21 spamd[27698]: 91.238.181.91: disconnected after 2 seconds.
Jun  9 23:59:53 192.168.10.21 spamd[27698]: 91.238.181.91: connected (2/0)
Jun  9 23:59:54 192.168.10.21 spamd[27698]: 91.238.181.91: disconnected after 4 seconds.
Jun  9 23:59:57 192.168.10.21 spamd[27698]: 91.238.181.91: disconnected after 4 seconds.
MATCH [23.248.217.56] with 6 lines
Opening [~/data/local7.log] for read
Jun  9 02:37:01 honeypi00 sensor: PacketTime:2025-06-09 09:37:01.205778 Len:62 IPv4/TCP 23.248.217.56:4214 -> 10.10.10.40:80 ID:55780 TOS:0x0 TTL:111 IpLen:20 DgLen:48 ****S* Seq:0xa9d343cf Ack:0x0 Win:0x2000 TcpLen:28 Resp:SA Packetdata:3KYyaJoq1Haglac3CABFAAAw2eRAAG8GLIEX+Nk4CgoKKBB2AFCp00PPAAAAAHACIABfVAAAAgQFtAEBBAIO
Jun  9 02:37:01 honeypi00 sensor: PacketTime:2025-06-09 09:37:01.424181 Len:60 IPv4/TCP 23.248.217.56:4214 -> 10.10.10.40:80 ID:22927 TOS:0x0 TTL:43 IpLen:20 DgLen:40 ***R** Seq:0xa9d343d0 Ack:0x0 Win:0x0 TcpLen:20 Resp: Packetdata:3KYyaJoq1Haglac3CABFAAAoWY9AACsG8N4X+Nk4CgoKKBB2AFCp00PQAAAAAFAEAACsFAAAAAAAAAAADg==
Jun  9 02:37:07 192.168.10.21 date=2025-06-09 time=02:37:07 devname="FortiWiFi-80F-2R" devid="FWF80FTK21002176" eventtime=1749461827438416270 tz="-0700" logid="0000000013" type="traffic" subtype="forward" level="notice" vd="root" srcip=23.248.217.56 srcport=4214 srcintf="wan2" srcintfrole="wan" dstip=99.88.84.61 dstport=80 dstintf="BLACKHOLE" dstintfrole="dmz" srccountry="United States" dstcountry="United States" sessionid=4327215 proto=6 action="client-rst" policyid=46 policytype="policy" poluuid="1d2c8168-c62e-51ea-8f55-d563c17af0df" policyname="WAN->HOLE-HONEYPI_ALL" service="HTTP" trandisp="dnat" tranip=10.10.10.40 tranport=80 appcat="unscanned" duration=6 sentbyte=88 rcvdbyte=40 sentpkt=2 rcvdpkt=1 dsthwvendor="Raspberry Pi" masterdstmac="dc:a6:32:68:9a:2a" dstmac="dc:a6:32:68:9a:2a" dstserver=0
Jun  9 05:54:02 honeypi00 sensor: PacketTime:2025-06-09 12:54:02.574892 Len:62 IPv4/TCP 23.248.217.56:25224 -> 10.10.10.40:80 ID:45973 TOS:0x0 TTL:113 IpLen:20 DgLen:48 ****S* Seq:0xe907ac57 Ack:0x0 Win:0x2000 TcpLen:28 Resp:SA Packetdata:3KYyaJoq1Haglac3CABFAAAws5VAAHEGUNAX+Nk4CgoKKGKIAFDpB6xXAAAAAHACIABlhQAAAgQFtAEBBAIO
Jun  9 05:54:02 honeypi00 sensor: PacketTime:2025-06-09 12:54:02.749473 Len:60 IPv4/TCP 23.248.217.56:25224 -> 10.10.10.40:80 ID:14822 TOS:0x0 TTL:43 IpLen:20 DgLen:40 ***R** Seq:0xe907ac58 Ack:0x0 Win:0x0 TcpLen:20 Resp: Packetdata:3KYyaJoq1Haglac3CABFAAAoOeZAACsGEIgX+Nk4CgoKKGKIAFDpB6xYAAAAAFAEAACyRQAAAAAAAAAADg==
Jun  9 05:54:09 192.168.10.21 date=2025-06-09 time=05:54:09 devname="FortiWiFi-80F-2R" devid="FWF80FTK21002176" eventtime=1749473648788417590 tz="-0700" logid="0000000013" type="traffic" subtype="forward" level="notice" vd="root" srcip=23.248.217.56 srcport=25224 srcintf="wan2" srcintfrole="wan" dstip=99.88.84.61 dstport=80 dstintf="BLACKHOLE" dstintfrole="dmz" srccountry="United States" dstcountry="United States" sessionid=4476397 proto=6 action="client-rst" policyid=46 policytype="policy" poluuid="1d2c8168-c62e-51ea-8f55-d563c17af0df" policyname="WAN->HOLE-HONEYPI_ALL" service="HTTP" trandisp="dnat" tranip=10.10.10.40 tranport=80 appcat="unscanned" duration=6 sentbyte=88 rcvdbyte=40 sentpkt=2 rcvdpkt=1 dsthwvendor="Raspberry Pi" masterdstmac="dc:a6:32:68:9a:2a" dstmac="dc:a6:32:68:9a:2a" dstserver=0

real	0m0.032s
user	0m0.014s
sys	0m0.018s
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

### Command Line Options

```sh
$ logpi --help
logpi v0.11 [Aug 24 2025 - 13:08:54]

Log Pseudo Indexer - High-performance network address extraction and indexing

syntax: logpi [options] filename [filename ...]

Options:
 -d|--debug (0-9)       enable debugging info (0=none, 9=verbose)
 -g|--greedy            ignore quotes when parsing fields
 -h|--help              display this help information
 -s|--serial            force serial processing (disable parallel mode)
 -v|--version           display version information
 -w|--write             auto-generate .lpi files for each input file

Arguments:
 filename               one or more log files to process
                        use '-' to read from stdin (not compatible with -w)

Performance Features:
 - Automatic parallel processing for files >100MB
 - Multi-threaded architecture with dedicated I/O and hash threads
 - Optimized for IPv4, IPv6, and MAC address extraction
 - Serial processing: ~60M lines/minute, Parallel: 125M+ lines/minute
 - Serial mode available for debugging or memory-constrained systems

Output Format:
 Without -w: Network addresses printed to stdout
 With -w:    Creates .lpi index files (input.log -> input.log.lpi)
 Index format: ADDRESS,COUNT,LINE:FIELD,LINE:FIELD,...

Examples:
 logpi -w /var/log/syslog                    # Create syslog.lpi index
 logpi -d 1 -w *.log                        # Process all .log files with debug
 logpi -s -w huge_file.log                  # Force serial processing for large file
 tail -f /var/log/access.log | logpi -      # Real-time processing from stdin
```

### Building and Installation

```sh
# Initialize the build system (first time only)
./bootstrap

# Configure the project
./configure

# Build the project  
make

# Install (optional)
sudo make install
```

### Usage Examples

Running the command line tool is simple:

```sh
logpi /var/log/syslog
Opening [/var/log/syslog] for read
9a:65:c3:fe:9f:27,4,113014:2,112957:2,112949:2,112942:2
1e:2c:33:94:40:8d,1,81650:2
1e:91:33:0c:d0:3d,4,103250:2,103136:2,103026:2,103011:2
82:32:e9:5a:62:b2,2,41977:2,41733:2
56:be:d8:99:85:7f,3,24912:2,24692:2,24685:2
a6:46:7f:9d:d1:38,1,52455:2
0.0.2.42,6,96811:1,96788:1,82631:1,47211:1,46105:1,688:1
a2:e6:cc:e1:11:06,4,78438:2,78430:2,78422:2,78224:2
1a:76:0d:f3:8d:cc,1,11545:2
16:90:c3:0a:4f:59,2,97271:2,97264:2
7a:bc:0c:cd:ff:d6,1,86037:2
d6:8f:0a:10:e7:71,6,41223:2,41133:2,41123:2,41116:2,41109:2,41096:2
7e:10:be:76:e5:f4,3,16569:2,16382:2,16375:2
4a:bb:9d:33:7d:81,1,87827:2
e2:21:c7:4b:b1:12,4,66545:2,66355:2,66348:2,66332:2
62:a7:29:49:da:f3,1,88595:2
172.20.1.100,92,115891:1,115274:1,113296:1,112657:1,110689:1,110052:1,107922:1,107411:1,105751:1,105254:1,103563:1,103043:1,101424:1,100921:1,98970:1,98346:1,95237:1,94609:1,92513:1,91877:1,89637:1,89090:1,87367:1,86815:1,85119:1,84599:1,82976:1,81291:1,80769:1,78958:1,78408:1,76775:1,76242:1,74525:1,73924:1,72200:1,71644:1,69899:1,69390:1,67707:1,67180:1,65421:1,64882:1,63068:1,62530:1,60822:1,60208:1,57288:1,56325:1,53794:1,53159:1,51177:1,50507:1,48443:1,47901:1,44708:1,42918:1,42329:1,40346:1,39628:1,37889:1,37254:1,35437:1,34883:1,33038:1,32487:1,30712:1,30156:1,28253:1,27575:1,25785:1,25200:1,23359:1,22781:1,20841:1,20318:1,18353:1,17792:1,16104:1,15593:1,13881:1,13369:1,11666:1,11135:1,9459:1,8934:1,7234:1,6684:1,4982:1,4417:1,2630:1,2033:1
82:6b:bb:fb:eb:5d,8,73800:2,73794:2,73787:2,73778:2,73768:2,73760:2,73753:2,73549:2
9a:0b:20:44:ba:99,2,29803:2,29793:2
8e:92:fa:3a:69:e2,1,106535:2
36:a2:de:5a:83:2d,2,24616:2,24394:2
9a:6a:aa:4d:5e:3f,1,85498:2
00:00:00:00:00:00,3,62933:1,7090:1,6951:1
d6:99:c5:5e:5b:94,1,4628:2
```

### Output Format

The pseudo-index files (.lpi) are text files with carriage-return delimited records. Each record follows this format:

```
{NETWORK_ADDRESS},{OCCURRENCE_COUNT},{LINE_NUMBER}:{FIELD_OFFSET},...
```

For example:
- `192.168.1.100,5,1001:2,2045:1,3012:2,4001:1,5500:3` means IP address 192.168.1.100 appears 5 times:
  - Line 1001, field 2
  - Line 2045, field 1  
  - Line 3012, field 2
  - Line 4001, field 1
  - Line 5500, field 3

### Searching with SearchPI (spi)

Searching using the pseudo indexes is simplified by using the `searchpi` (spi) command:

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

## Recent Performance Improvements (2025)

Version 0.10 introduces significant performance enhancements:

### New Parallel Processing Architecture
- **Eliminated Hash Bottlenecks**: Replaced multiple competing hash tables with a single dedicated hash management thread
- **True Parallelism**: Independent I/O, parser, and hash threads with lock-free communication queues
- **Automatic Scaling**: Automatically detects large files (>100MB) and switches to parallel mode
- **Memory Bounded**: Streaming chunk processing prevents memory exhaustion on large files

### Performance Gains
- **Hash Performance**: Eliminated 604+ million competing hash table lookups identified via profiling
- **I/O Efficiency**: Dedicated I/O thread with optimized chunk size (128MB) prevents I/O blocking
- **Memory Usage**: Increased initial hash table size to 65K buckets to reduce collision chains
- **Thread Efficiency**: Lock-free producer-consumer queues eliminate thread contention

### Results
- Successfully processes multi-GB files without crashes or hangs  
- Serial mode: ~60M lines/minute, Parallel mode: 125M+ lines/minute throughput
- Real-time progress reporting every 60 seconds during parallel processing
- Identical index files between serial and parallel modes (verified byte-for-byte)
- Zero performance degradation for smaller files that use serial processing

These improvements make logpi suitable for processing the largest log files found in enterprise environments while maintaining the simplicity and speed that made it valuable for security analysis.

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
