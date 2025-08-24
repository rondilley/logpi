/*****
 *
 * Description: Main Functions
 *
 * Copyright (c) 2008-2025, Ron Dilley
 * All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 ****/

/****
 *
 * includes
 *.
 ****/

#include "lpi_main.h"

/****
 *
 * local variables
 *
 ****/

/****
 *
 * global variables
 *
 ****/

PUBLIC volatile int quit = FALSE;
PUBLIC int reload = FALSE;
PUBLIC Config_t *config = NULL;

/****
 *
 * external variables
 *
 ****/

extern int errno;
extern char **environ;

/****
 *
 * main function
 *
 ****/

int main(int argc, char *argv[]) {
  FILE *inFile = NULL, *outFile = NULL;
  char inBuf[8192];
  char outFileName[PATH_MAX];
  PRIVATE int c = 0, i, ret;

#ifndef DEBUG
  struct rlimit rlim;

  rlim.rlim_cur = rlim.rlim_max = 0;
  setrlimit(RLIMIT_CORE, &rlim);
#endif

  /* setup config */
  config = (Config_t *)XMALLOC(sizeof(Config_t));
  XMEMSET(config, 0, sizeof(Config_t));

  /* force mode to forground */
  config->mode = MODE_INTERACTIVE;

  /* store current pid */
  config->cur_pid = getpid();

  /* get real uid and gid in prep for priv drop */
  config->gid = getgid();
  config->uid = getuid();

  while (1) {
    int this_option_optind = optind ? optind : 1;
#ifdef HAVE_GETOPT_LONG
    int option_index = 0;
    static struct option long_options[] = {
        {"greedy", no_argument, 0, 'g'},      {"version", no_argument, 0, 'v'},
        {"debug", required_argument, 0, 'd'}, {"help", no_argument, 0, 'h'},
        {"write", no_argument, 0, 'w'}, {"serial", no_argument, 0, 's'}, 
        {0, no_argument, 0, 0}};
    c = getopt_long(argc, argv, "vd:hwgs", long_options, &option_index);
#else
    c = getopt(argc, argv, "vd:hwgs");
#endif

    if (c EQ - 1)
      break;

    switch (c) {

    case 'v':
      /* show the version */
      print_version();
      return (EXIT_SUCCESS);

    case 'd':
      /* show debug info */
      if (optarg && strlen(optarg) > 0) {
        char *endptr;
        long debug_level = strtol(optarg, &endptr, 10);
        
        /* Check for valid conversion and range */
        if (*endptr != '\0' || endptr == optarg) {
          display(LOG_ERR, "Invalid debug level format");
          return (EXIT_FAILURE);
        }
        
        if (debug_level >= 0 && debug_level <= 9) {
          config->debug = (int)debug_level;
        } else {
          display(LOG_ERR, "Debug level must be between 0-9");
          return (EXIT_FAILURE);
        }
      } else {
        display(LOG_ERR, "Debug level required");
        return (EXIT_FAILURE);
      }
      break;

    case 'g':
      /* ignore quotes */
      config->greedy = TRUE;
      break;

    case 'h':
      /* show help info */
      print_help();
      return (EXIT_SUCCESS);

    case 'w':
      /* enable automatic .lpi file naming */
      config->auto_lpi_naming = TRUE;
      break;

    case 's':
      /* force serial processing */
      config->force_serial = TRUE;
      break;

    default:
      fprintf(stderr, "Unknown option code [0%o]\n", c);
    }
  }

  /* check dirs and files for danger */

  if (time(&config->current_time) EQ - 1) {
    display(LOG_ERR, "Unable to get current time");

    /* cleanup buffers */
    cleanup();
    return (EXIT_FAILURE);
  }

  /* initialize program wide config options */
  config->hostname = (char *)XMALLOC(MAXHOSTNAMELEN + 1);

  /* get processor hostname */
  if (gethostname(config->hostname, MAXHOSTNAMELEN) != 0) {
    display(LOG_ERR, "Unable to get hostname");
    strncpy(config->hostname, "unknown", MAXHOSTNAMELEN - 1);
    config->hostname[MAXHOSTNAMELEN - 1] = '\0';
  }

  config->cur_pid = getpid();

  /* setup current time updater */
  signal(SIGALRM, ctime_prog);
  alarm(ALARM_TIMER);

  /*
   * get to work
   */

  /* Check for stdin + -w combination error */
  if (config->auto_lpi_naming) {
    for (int i = optind; i < argc; i++) {
      if (strcmp(argv[i], "-") == 0) {
        fprintf(stderr, "ERR - Cannot use -w switch when reading from stdin\n");
        cleanup();
        return (EXIT_FAILURE);
      }
    }
  }

  /* process all the files */
  while (optind < argc) {
    /* Validate file path for security */
    if (!is_path_safe(argv[optind])) {
      display(LOG_ERR, "Unsafe file path rejected: %s", argv[optind]);
      optind++;
      continue;
    }
    processFile(argv[optind++]);
  }

  /* show addresses (only if not using auto-naming) */
  if (!config->auto_lpi_naming) {
    showAddresses();
  }

  /*
   * finished with the work
   */

  cleanup();

  return (EXIT_SUCCESS);
}

/****
 *
 * display prog info
 *
 ****/

void show_info(void) {
  fprintf(stderr, "%s v%s [%s - %s]\n", PROGNAME, VERSION, __DATE__, __TIME__);
  fprintf(stderr, "By: Ron Dilley\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "%s comes with ABSOLUTELY NO WARRANTY.\n", PROGNAME);
  fprintf(stderr, "This is free software, and you are welcome\n");
  fprintf(stderr, "to redistribute it under certain conditions;\n");
  fprintf(stderr, "See the GNU General Public License for details.\n");
  fprintf(stderr, "\n");
}

/*****
 *
 * display version info
 *
 *****/

PRIVATE void print_version(void) {
  printf("%s v%s [%s - %s]\n", PROGNAME, VERSION, __DATE__, __TIME__);
}

/*****
 *
 * print help info
 *
 *****/

PRIVATE void print_help(void) {
  print_version();

  fprintf(stderr, "\n");
  fprintf(stderr, "Log Pseudo Indexer - High-performance network address extraction and indexing\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "syntax: %s [options] filename [filename ...]\n", PACKAGE);
  fprintf(stderr, "\n");
  fprintf(stderr, "Options:\n");

#ifdef HAVE_GETOPT_LONG
  fprintf(stderr, " -d|--debug (0-9)       enable debugging info (0=none, 9=verbose)\n");
  fprintf(stderr, " -g|--greedy            ignore quotes when parsing fields\n");
  fprintf(stderr, " -h|--help              display this help information\n");
  fprintf(stderr, " -s|--serial            force serial processing (disable parallel mode)\n");
  fprintf(stderr, " -v|--version           display version information\n");
  fprintf(stderr, " -w|--write             auto-generate .lpi files for each input file\n");
#else
  fprintf(stderr, " -d {0-9}      enable debugging info (0=none, 9=verbose)\n");
  fprintf(stderr, " -g            ignore quotes when parsing fields\n");
  fprintf(stderr, " -h            display this help information\n");
  fprintf(stderr, " -s            force serial processing (disable parallel mode)\n");
  fprintf(stderr, " -v            display version information\n");
  fprintf(stderr, " -w            auto-generate .lpi files for each input file\n");
#endif

  fprintf(stderr, "\n");
  fprintf(stderr, "Arguments:\n");
  fprintf(stderr, " filename               one or more log files to process\n");
  fprintf(stderr, "                        use '-' to read from stdin (not compatible with -w)\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "Performance Features:\n");
  fprintf(stderr, " - Automatic parallel processing for files >100MB\n");
  fprintf(stderr, " - Multi-threaded architecture with dedicated I/O and hash threads\n");
  fprintf(stderr, " - Optimized for IPv4, IPv6, and MAC address extraction\n");
  fprintf(stderr, " - Serial processing: ~60M lines/minute, Parallel: 125M+ lines/minute\n");
  fprintf(stderr, " - Serial mode available for debugging or memory-constrained systems\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "Output Format:\n");
  fprintf(stderr, " Without -w: Network addresses printed to stdout\n");
  fprintf(stderr, " With -w:    Creates .lpi index files (input.log -> input.log.lpi)\n");
  fprintf(stderr, " Index format: ADDRESS,COUNT,LINE:FIELD,LINE:FIELD,...\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "Examples:\n");
  fprintf(stderr, " %s -w /var/log/syslog                    # Create syslog.lpi index\n", PACKAGE);
  fprintf(stderr, " %s -d 1 -w *.log                        # Process all .log files with debug\n", PACKAGE);
  fprintf(stderr, " %s -s -w huge_file.log                  # Force serial processing for large file\n", PACKAGE);
  fprintf(stderr, " tail -f /var/log/access.log | %s -      # Real-time processing from stdin\n", PACKAGE);
  fprintf(stderr, "\n");
}

/****
 *
 * cleanup
 *
 ****/

PRIVATE void cleanup(void) {
  /* free any match templates */
  cleanMatchList();

  if (config->outFile_st != NULL)
    fclose(config->outFile_st);
  XFREE(config->hostname);
#ifdef MEM_DEBUG
  XFREE_ALL();
#else
  XFREE(config);
#endif
}

/*****
 *
 * interrupt handler (current time)
 *
 *****/

void ctime_prog(int signo) {
  time_t ret;

  /* disable SIGALRM */
  signal(SIGALRM, SIG_IGN);
  /* update current time */
  reload = TRUE;

  /* reset SIGALRM */
  signal(SIGALRM, ctime_prog);
  /* reset alarm */
  alarm(ALARM_TIMER);
}
