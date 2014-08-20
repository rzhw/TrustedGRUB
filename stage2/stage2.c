/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2000,2001,2002,2004,2005  Free Software Foundation, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <shared.h>
#include <term.h>


grub_jmp_buf restart_env;
#define MASTER_CONFIG_FILE "(nd)/tftpboot/m"

#if defined(PRESET_MENU_STRING) || defined(SUPPORT_DISKLESS)

# if defined(PRESET_MENU_STRING)
static const char *preset_menu = PRESET_MENU_STRING;
# elif defined(SUPPORT_DISKLESS)
/* Execute the command "bootp" automatically.  */
static const char *preset_menu = "bootp\n";
# endif /* SUPPORT_DISKLESS */

static int preset_menu_offset;

static int
open_preset_menu (void)
{
#ifdef GRUB_UTIL
  /* Unless the user explicitly requests to use the preset menu,
     always opening the preset menu fails in the grub shell.  */
  if (! use_preset_menu)
    return 0;
#endif /* GRUB_UTIL */
  
  preset_menu_offset = 0;
  return preset_menu != 0;
}

static int
read_from_preset_menu (char *buf, int maxlen)
{
  int len = grub_strlen (preset_menu + preset_menu_offset);

  if (len > maxlen)
    len = maxlen;

  grub_memmove (buf, preset_menu + preset_menu_offset, len);
  preset_menu_offset += len;

  return len;
}

static void
close_preset_menu (void)
{
  /* Disable the preset menu.  */
  preset_menu = 0;
}

#else /* ! PRESET_MENU_STRING && ! SUPPORT_DISKLESS */

#define open_preset_menu()	0
#define read_from_preset_menu(buf, maxlen)	0
#define close_preset_menu()

#endif /* ! PRESET_MENU_STRING && ! SUPPORT_DISKLESS */

/* config_file is 128 Bytes long (see grub/asmstub.c) */
#define CONFIG_FILE_LEN 128
#define CONFIG_FILE_HISTORY_ENTRIES 10
struct config_file_history_struct {
  char filename[CONFIG_FILE_LEN];
  int entryno;
  int first_entry;
};
static struct config_file_history_struct
        config_file_history[CONFIG_FILE_HISTORY_ENTRIES];
static int config_file_history_pos, config_file_history_start,
	config_file_history_prev_pos;
static int config_file_history_menu_pos = -1;


static char *
get_entry (char *list, int num, int nested)
{
  int i;

  for (i = 0; i < num; i++)
    {
      do
	{
	  while (*(list++));
	}
      while (nested && *(list++));
    }

  return list;
}

/* Print an entry in a line of the menu box.  */
static void
print_entry (int y, int highlight, char *entry)
{
  int x;

  if (current_term->setcolorstate)
    current_term->setcolorstate (COLOR_STATE_NORMAL);
  
  if (highlight && current_term->setcolorstate)
    current_term->setcolorstate (COLOR_STATE_HIGHLIGHT);

  entry = var_sprint_buf(entry, &x);
  gotoxy (2, y);
  grub_putchar (' ');
  for (x = 3; x < 75; x++)
    {
      if (*entry && x <= 72)
	{
	  if (x == 72)
	    grub_putchar (DISP_RIGHT);
	  else
	    grub_putchar (*entry++);
	}
      else
	grub_putchar (' ');
    }
  gotoxy (74, y);

  if (current_term->setcolorstate)
    current_term->setcolorstate (COLOR_STATE_STANDARD);
}

/* Print entries in the menu box.  */
static void
print_entries (int y, int size, int first, int entryno, char *menu_entries)
{
  int i;
  
  gotoxy (77, y + 1);

  if (first)
    grub_putchar (DISP_UP);
  else
    grub_putchar (' ');

  menu_entries = get_entry (menu_entries, first, 0);

  for (i = 0; i < size; i++)
    {
      print_entry (y + i + 1, entryno == i, menu_entries);

      while (*menu_entries)
	menu_entries++;

      if (*(menu_entries - 1))
	menu_entries++;
    }

  gotoxy (77, y + size);

  if (*menu_entries)
    grub_putchar (DISP_DOWN);
  else
    grub_putchar (' ');

  gotoxy (74, y + entryno + 1);
}

static void
print_entries_raw (int size, int first, char *menu_entries)
{
  int i;

#define LINE_LENGTH 67

  for (i = 0; i < LINE_LENGTH; i++)
    grub_putchar ('-');
  grub_putchar ('\n');

  for (i = first; i < size; i++)
    {
      /* grub's printf can't %02d so ... */
      if (i < 10)
	grub_putchar (' ');
      grub_printf ("%d: %s\n", i, get_entry (menu_entries, i, 0));
    }

  for (i = 0; i < LINE_LENGTH; i++)
    grub_putchar ('-');
  grub_putchar ('\n');

#undef LINE_LENGTH
}


static void
print_border (int y, int size)
{
  int i;

  if (current_term->setcolorstate)
    current_term->setcolorstate (COLOR_STATE_NORMAL);
  
  gotoxy (1, y);

  grub_putchar (DISP_UL);
  for (i = 0; i < 73; i++)
    grub_putchar (DISP_HORIZ);
  grub_putchar (DISP_UR);

  i = 1;
  while (1)
    {
      gotoxy (1, y + i);

      if (i > size)
	break;
      
      grub_putchar (DISP_VERT);
      gotoxy (75, y + i);
      grub_putchar (DISP_VERT);

      i++;
    }

  grub_putchar (DISP_LL);
  for (i = 0; i < 73; i++)
    grub_putchar (DISP_HORIZ);
  grub_putchar (DISP_LR);

  if (current_term->setcolorstate)
    current_term->setcolorstate (COLOR_STATE_STANDARD);
}

static void
run_menu (char *menu_entries, char *config_entries, int num_entries,
	  char *heap, int entryno)
{
  int c, time1, time2 = -1, first_entry = 0;
  char *cur_entry = 0;
  char shortcut_buf[5];
  int sc_matches;
#define SEARCH_BUF_SIZE 20
  char search_buf[SEARCH_BUF_SIZE];
  int search_mode = 0, search_found = 0, search_direction = 0;


  /* nested function, we need the code in multiple places */
  void set_bar_to(int i)
    {
      first_entry = i - 5;
      entryno = 5;

      if (first_entry < 0 || num_entries < 13)
	{
	  entryno = i;
	  first_entry = 0;
	}
      else if (num_entries - i < 7)
	{
	  first_entry = num_entries - 12;
	  entryno = i - first_entry;
	}

      print_entries (3, 12, first_entry,
	             entryno, menu_entries);
    }

  /* search through menu_entries once around */
  int search_menu(char *buf, int current, int direction, int advance)
    {
      int i;
      /* make direction a delta */
      direction = (direction ? -1 : 1);

      if (advance)
	{
	  /* go to next search item */
	  current += direction;

	  /* correct overflows */
	  if (current < 0)
	    current = num_entries - 1;
	  else if (current == num_entries)
	    current = 0;
	}

      i = current;

      do
	{
	  int x;

	  /* get_entry is probably overkill here... */
	  char *s = var_sprint_buf(get_entry(menu_entries,
		                             i, 0), &x);
	  for (; *s; s++)
	    {
	      char *sb = buf;
	      char *ss = s;
	      /* incasesensitive search */
	      while (*ss && *sb && grub_tolower(*ss) == *sb)
		ss++, sb++;

	      if (!*sb) /* Found something! */
		{
		  set_bar_to(i);
		  return i;
		}
	    }

	  if (direction == -1 && i == 0)
	    i = num_entries;
	  i += direction;
	  if (direction == 1 && i == num_entries)
	    i = 0;
	}
      while (i != current);

      /* Found nothing */
      return -1;
    }


  *shortcut_buf = *search_buf = 0;

  /*
   *  Main loop for menu UI.
   */

restart:

  if (config_file_history_menu_pos != -1)
    {
      /* we're in a history back movement, set menu values to previous ones */
      entryno     = config_file_history[config_file_history_menu_pos].entryno;
      first_entry = config_file_history[config_file_history_menu_pos].first_entry;
      config_file_history_menu_pos = -1;
    }
  
  /* Dumb terminal always use all entries for display 
     invariant for TERM_DUMB: first_entry == 0  */
  if (! (current_term->flags & TERM_DUMB))
    {
      while (entryno > 11)
	{
	  first_entry++;
	  entryno--;
	}
    }

  /* If the timeout was expired or wasn't set, force to show the menu
     interface. */
  if (grub_timeout < 0)
    show_menu = 1;
  
  /* If SHOW_MENU is false, don't display the menu until ESC is pressed.  */
  if (! show_menu)
    {
      /* Get current time.  */
      while ((time1 = getrtsecs ()) == 0xFF)
	;

      while (1)
	{
	  /* Check if ESC is pressed.  */
	  if (checkkey () != -1 && ASCII_CHAR (getkey ()) == '\e')
	    {
	      grub_timeout = -1;
	      show_menu = 1;
	      break;
	    }

	  /* If GRUB_TIMEOUT is expired, boot the default entry.  */
	  if (grub_timeout >=0
	      && (time1 = getrtsecs ()) != time2
	      && time1 != 0xFF)
	    {
	      if (grub_timeout <= 0)
		{
		  grub_timeout = -1;
		  goto boot_entry;
		}
	      
	      time2 = time1;
	      grub_timeout--;
	      
	      /* Print a message.  */
	      grub_printf ("\rPress `ESC' to enter the menu... %d   ",
			   grub_timeout);
	    }
	}
    }

  /* Only display the menu if the user wants to see it. */
  if (show_menu)
    {
      init_page ();
      setcursor (0);

      if (current_term->flags & TERM_DUMB)
	print_entries_raw (num_entries, first_entry, menu_entries);
      else
	print_border (3, 12);

#if 0
      grub_printf ("\n\
      Use the %c and %c keys to select which entry is highlighted.\n",
		   DISP_UP, DISP_DOWN);
#endif
      
      if (! auth && password)
	{
	  printf ("\
      Press enter to boot the selected OS or \'p\' to enter a\n\
      password to unlock the next set of features.");
	}
      else
	{
	  if (config_entries)
	  {
	    if (!toggle_print_status(3, 18))
	      {
		printf ("\n\n\
      Press enter or %c to boot the selected OS, \'e\' to edit the\n\
      commands before booting, \'r\' to reload, \'c\' for a command-line,\n\
      \'/?nN\' to search or %c to go back if possible.",
                        DISP_RIGHT, DISP_LEFT);
	      }
	  }
	  else
	    printf ("\
      Press \'b\' to boot, \'e\' to edit the selected command in the\n\
      boot sequence, \'c\' for a command-line, \'o\' to open a new line\n\
      after (\'O\' for before) the selected line, \'d\' to remove the\n\
      selected line, \'/?nN\' to search, or escape to go back to the main menu.");
	}

      if (current_term->flags & TERM_DUMB)
	grub_printf ("\n\nThe selected entry is %d ", entryno);
      else
	print_entries (3, 12, first_entry, entryno, menu_entries);
    }

  /* XX using RT clock now, need to initialize value */
  while ((time1 = getrtsecs()) == 0xFF);

  while (1)
    {
      /* Initialize to NULL just in case...  */
      cur_entry = NULL;

      if (grub_timeout >= 0 && (time1 = getrtsecs()) != time2 && time1 != 0xFF)
	{
	  if (grub_timeout <= 0)
	    {
	      grub_timeout = -1;
	      break;
	    }

	  /* else not booting yet! */
	  time2 = time1;

	  if (current_term->flags & TERM_DUMB)
	      grub_printf ("\r    Entry %d will be booted automatically in %d seconds.   ", 
			   entryno, grub_timeout);
	  else
	    {
	      gotoxy (3, 22);
	      grub_printf ("The highlighted entry will be booted automatically in %d seconds.    ",
			   grub_timeout);
	      gotoxy (74, 4 + entryno);
	  }
	  
	  grub_timeout--;
	}

menu_restart:

      /* Print the number of the current entry in the right upper corner of
       * the menu, up to 999 entries are supported, modify the coordinates
       * and putchar command to add more
       * Additionally, print the shortcut buffer upper left if there's
       * something in there */
      if (! (current_term->flags & TERM_DUMB))
        {
	  int x = 'x', i, l;

	  if (current_term->setcolorstate)
	    current_term->setcolorstate (COLOR_STATE_NORMAL);

          /* current entry */
          gotoxy(69, 3);
          grub_printf("[%d]", first_entry + entryno);
          grub_putchar(DISP_HORIZ);
          grub_putchar(DISP_HORIZ);


          /* print shortcut buffer */
          gotoxy(5, 3);
	  if (search_mode)
	    grub_printf("%c%s%c%c%c%c",
		        x, search_buf, x = search_direction ? '?' : '/',
			DISP_HORIZ,
			search_found >= 0 ? DISP_HORIZ : 'X', DISP_HORIZ);
	  else if (shortcut_buf[0])
            grub_printf("<%s..>", shortcut_buf);
          else
            for (x = 0; x < 24; x++)
              grub_putchar(DISP_HORIZ);

	  gotoxy(52, 16);
	  l = grub_strlen(search_buf);
	  for (i = SEARCH_BUF_SIZE + 2 -
	          ((search_found >= 0 && (l || search_mode)) ? l + 2 : 0);
	       i; i--)
	    grub_putchar(DISP_HORIZ);
	  if (search_found >= 0 && (l || search_mode))
	    {
	      x = search_direction ? '?' : '/';
	      grub_putchar(x);
	      for (i = 0; i < l; i++)
		grub_putchar(search_buf[i]);
	      grub_putchar(x);
	    }

	  if (current_term->setcolorstate)
	    current_term->setcolorstate (COLOR_STATE_STANDARD);

          gotoxy(74, 4 + entryno);
        }


      /* Check for a keypress, however if TIMEOUT has been expired
	 (GRUB_TIMEOUT == -1) relax in GETKEY even if no key has been
	 pressed.  
	 This avoids polling (relevant in the grub-shell and later on
	 in grub if interrupt driven I/O is done).  */
      if (checkkey () >= 0 || grub_timeout < 0)
	{
	  /* Key was pressed, show which entry is selected before GETKEY,
	     since we're comming in here also on GRUB_TIMEOUT == -1 and
	     hang in GETKEY */
	  if (current_term->flags & TERM_DUMB)
	    grub_printf ("\r    Highlighted entry is %d: ", entryno);

	  c = ASCII_CHAR (getkey ());

	  if (grub_timeout >= 0)
	    {
	      if (current_term->flags & TERM_DUMB)
		grub_putchar ('\r');
	      else
		gotoxy (3, 22);
	      printf ("                                                                    ");
	      grub_timeout = -1;
	      fallback_entryno = -1;
	      if (! (current_term->flags & TERM_DUMB))
		gotoxy (74, 4 + entryno);
	    }

	  if (search_mode)
	    {
	      int inplen = grub_strlen(search_buf);

	      if (c == '\r' || c == '\n' || c == 27)
		{
		  search_mode = 0;
		  goto menu_restart;
		}
	      else if (c != 8 && c < ' ') /* any other "move around" key */
	        {
		  search_mode = 0;
		  /* fall through to other keys */
		}
	      else
		{
		  if (c == 8) /* Backspace */
		    {
		      if (!inplen)
			search_mode = 0;
		      else
			search_buf[--inplen] = 0;
		    }
		  else if (inplen < sizeof(search_buf) - 1)
		    {
		      search_buf[inplen]   = grub_tolower(c);
		      search_buf[++inplen] = 0;
		    }

		  if (search_mode)
		    search_found = search_menu(search_buf,
			                       first_entry + entryno,
					       search_direction, 0);

	          goto menu_restart;
		}
	    }
	  else if (c == '/' || c == '?')
	    {
	      search_mode = 1;
	      search_direction = (c == '?');
	      *search_buf = search_found = 0;
	    }

	  if (c == 'n') /* search again forwards */
	    if (search_found >= 0)
	      search_menu(search_buf, first_entry + entryno,
		          search_direction, 1);

	  if (c == 'N') /* search again backwards */
	    if (search_found >= 0)
	      search_menu(search_buf, first_entry + entryno,
		          !search_direction, 1);

	  if (c >= '0' && c <= '9')
	    {
	      int inplen = grub_strlen(shortcut_buf);
	      int i;

	      sc_matches = 0;

	      shortcut_buf[inplen]   = c;
	      shortcut_buf[++inplen] = 0;

	      for (i = 0; i < num_entries; i++)
		{
		  char buf[4];
		  int a = 0;

		  /* no strncmp in grub? do it ourselves */
		  /* If shortcut_buf is entirely in the beginning
		   * of buf, mark it as the first valid entry,
		   * if the first entry is already set, we have at least
		   * two entries matching, bail out then */
		  grub_sprintf(buf, "%d", i);
		  while (shortcut_buf[a] && buf[a] &&
		      shortcut_buf[a] == buf[a])
		    a++;

		  if (a == inplen)
		    {
		      sc_matches++;

		      if (sc_matches == 1)
			set_bar_to(i);
		      else
			break;
		    }
		}
	      if (sc_matches <= 1)
		shortcut_buf[0] = 0;
	      if (sc_matches == 1 && config_entries)
		c = '\n'; /* Will hit the next check */
	    }
	  else
	    shortcut_buf[0] = sc_matches = 0;

	  /* We told them above (at least in SUPPORT_SERIAL) to use
	     '^' or 'v' so accept these keys.  */
	  if (c == 16 || c == '^' || c == 'k')
	    {
	      if (current_term->flags & TERM_DUMB)
		{
		  if (entryno > 0)
		    entryno--;
		}
	      else
		{
		  if (entryno > 0)
		    {
		      print_entry (4 + entryno, 0,
				   get_entry (menu_entries,
					      first_entry + entryno,
					      0));
		      entryno--;
		      print_entry (4 + entryno, 1,
				   get_entry (menu_entries,
					      first_entry + entryno,
					      0));
		    }
		  else if (first_entry > 0)
		    {
		      first_entry--;
		      print_entries (3, 12, first_entry, entryno,
				     menu_entries);
		    }
		}
	    }
	  else if ((c == 14 || c == 'v' || c == 'j')
		   && first_entry + entryno + 1 < num_entries)
	    {
	      if (current_term->flags & TERM_DUMB)
		entryno++;
	      else
		{
		  if (entryno < 11)
		    {
		      print_entry (4 + entryno, 0,
				   get_entry (menu_entries,
					      first_entry + entryno,
					      0));
		      entryno++;
		      print_entry (4 + entryno, 1,
				   get_entry (menu_entries,
					      first_entry + entryno,
					      0));
		  }
		else if (num_entries > 12 + first_entry)
		  {
		    first_entry++;
		    print_entries (3, 12, first_entry, entryno, menu_entries);
		  }
		}
	    }
	  else if (c == 7)
	    {
	      /* Page Up */

	      if (first_entry > 11)
		{
		  first_entry -= 12;
		  print_entries (3, 12, first_entry, entryno, menu_entries);
		}
	      else if (first_entry)
		{
		  if (entryno + first_entry - 12 < 0)
		    entryno = 0;
		  else
		    entryno = first_entry + entryno - 12;
		  first_entry = 0;
		  print_entries (3, 12, first_entry, entryno, menu_entries);
		}
	      else if (entryno)
		{
		  print_entry (4 + entryno, 0,
			       get_entry (menu_entries,
				 	  first_entry + entryno,
					  0));
		  entryno = 0;
		  print_entry (4, 1,
			       get_entry (menu_entries,
				 	  first_entry,
					  0));
		}

#if 0
	      first_entry -= 12;
	      if (first_entry < 0)
		{
		  entryno += first_entry;
		  first_entry = 0;
		  if (entryno < 0)
		    entryno = 0;
		}
	      print_entries (3, 12, first_entry, entryno, menu_entries);
#endif
	    }
	  else if (c == 3)
	    {
	      /* Page Down */
	      if (first_entry + 12 < num_entries)
		{
		  if (first_entry + 23 < num_entries)
		    first_entry += 12;
		  else
		    {
		      if (entryno + first_entry + 12 >= num_entries)
			entryno = 11;
		      else
			entryno += 24 + first_entry - num_entries;
		      first_entry = num_entries - 12;
		    }
		  print_entries (3, 12, first_entry, entryno, menu_entries);
		}
	      else if (first_entry + entryno + 1 != num_entries)
		{
		  print_entry (4 + entryno, 0,
			       get_entry (menu_entries,
					  first_entry + entryno,
					  0));
		  entryno = num_entries - first_entry - 1;
		  print_entry (4 + entryno, 1,
			       get_entry (menu_entries,
					  first_entry + entryno,
					  0));
		}

#if 0
	      first_entry += 12;
	      if (first_entry + entryno + 1 >= num_entries)
		{
		  first_entry = num_entries - 12;
		  if (first_entry < 0)
		    first_entry = 0;
		  entryno = num_entries - first_entry - 1;
		}
	      print_entries (3, 12, first_entry, entryno, menu_entries);
#endif
	    }

	  if (c == 'M')
	    {
	      memmove(config_file, MASTER_CONFIG_FILE,
		      grub_strlen(MASTER_CONFIG_FILE) + 1);
	      return;
	    }

	  if (config_entries)
	    {
	      if (c == 'r')
		return;

	      if (c == '\n' || c == '\r' || c == 6 || c == 'l')
		{
		  config_file_history[config_file_history_prev_pos].entryno =
		    entryno;
		  config_file_history[config_file_history_prev_pos].first_entry =
		    first_entry;

		  break;
                } 

	      if (c == 2 || c == 'h') /* KEY_LEFT */ 
		{                      
		  /* go back in history if possible */
		  int p = config_file_history_prev_pos;
		  if (p != config_file_history_start)
		    {
		      p = (p == 0) ? CONFIG_FILE_HISTORY_ENTRIES - 1 : p - 1;
		      memmove(config_file, config_file_history[p].filename,
			  CONFIG_FILE_LEN);
		      config_file_history_pos = p;
		      config_file_history_menu_pos = p;

		      return;
		    }
		}
	    }
	  else
	    {
	      if ((c == 'd') || (c == 'o') || (c == 'O'))
		{
		  if (! (current_term->flags & TERM_DUMB))
		    print_entry (4 + entryno, 0,
				 get_entry (menu_entries,
					    first_entry + entryno,
					    0));

		  /* insert after is almost exactly like insert before */
		  if (c == 'o')
		    {
		      /* But `o' differs from `O', since it may causes
			 the menu screen to scroll up.  */
		      if (entryno < 11 || (current_term->flags & TERM_DUMB))
			entryno++;
		      else
			first_entry++;
		      
		      c = 'O';
		    }

		  cur_entry = get_entry (menu_entries,
					 first_entry + entryno,
					 0);

		  if (c == 'O')
		    {
		      grub_memmove (cur_entry + 2, cur_entry,
				    ((int) heap) - ((int) cur_entry));

		      cur_entry[0] = ' ';
		      cur_entry[1] = 0;

		      heap += 2;

		      num_entries++;
		    }
		  else if (num_entries > 0)
		    {
		      char *ptr = get_entry(menu_entries,
					    first_entry + entryno + 1,
					    0);

		      grub_memmove (cur_entry, ptr,
				    ((int) heap) - ((int) ptr));
		      heap -= (((int) ptr) - ((int) cur_entry));

		      num_entries--;

		      if (entryno >= num_entries)
			entryno--;
		      if (first_entry && num_entries < 12 + first_entry)
			first_entry--;
		    }

		  if (current_term->flags & TERM_DUMB)
		    {
		      grub_printf ("\n\n");
		      print_entries_raw (num_entries, first_entry,
					 menu_entries);
		      grub_printf ("\n");
		    }
		  else
		    print_entries (3, 12, first_entry, entryno, menu_entries);
		}

	      cur_entry = menu_entries;
	      if (c == 27)
		return;
	      if (c == 'b')
		break;
	    }

	  if (! auth && password)
	    {
	      if (c == 'p')
		{
		  /* Do password check here! */
		  char entered[32];
		  char *pptr = password;

		  if (current_term->flags & TERM_DUMB)
		    grub_printf ("\r                                    ");
		  else
		    gotoxy (1, 21);

		  /* Wipe out the previously entered password */
		  grub_memset (entered, 0, sizeof (entered));
		  get_cmdline (" Password: ", entered, 31, '*', 0);

		  while (! isspace (*pptr) && *pptr)
		    pptr++;

		  /* Make sure that PASSWORD is NUL-terminated.  */
		  *pptr++ = 0;

		  if (! check_password (entered, password, password_type))
		    {
		      char *new_file = config_file;
		      while (isspace (*pptr))
			pptr++;

		      /* If *PPTR is NUL, then allow the user to use
			 privileged instructions, otherwise, load
			 another configuration file.  */
		      if (*pptr != 0)
			{
			  while ((*(new_file++) = *(pptr++)) != 0)
			    ;

			  /* Make sure that the user will not have
			     authority in the next configuration.  */
			  auth = 0;
			  return;
			}
		      else
			{
			  /* Now the user is superhuman.  */
			  auth = 1;
			  goto restart;
			}
		    }
		  else
		    {
		      grub_printf ("Failed!\n      Press any key to continue...");
		      getkey ();
		      goto restart;
		    }
		}
	    }
	  else
	    {
	      if (c == 'e')
		{
		  int new_num_entries = 0, i = 0;
		  char *new_heap;

		  if (config_entries)
		    {
		      new_heap = heap;
		      cur_entry = get_entry (config_entries,
					     first_entry + entryno,
					     1);
		    }
		  else
		    {
		      /* safe area! */
		      new_heap = heap + NEW_HEAPSIZE + 1;
		      cur_entry = get_entry (menu_entries,
                                             first_entry + entryno,
					     0);
		    }

		  do
		    {
		      while ((*(new_heap++) = cur_entry[i++]) != 0);
		      new_num_entries++;
		    }
		  while (config_entries && cur_entry[i]);

		  /* this only needs to be done if config_entries is non-NULL,
		     but it doesn't hurt to do it always */
		  *(new_heap++) = 0;

		  if (config_entries)
		    run_menu (heap, NULL, new_num_entries, new_heap, 0);
		  else
		    {
		      cls ();
		      print_cmdline_message (0);

		      new_heap = heap + NEW_HEAPSIZE + 1;

		      saved_drive = boot_drive;
		      saved_partition = install_partition;
		      current_drive = GRUB_INVALID_DRIVE;

		      if (is_var_expand())
			{
			  int _s;
			  new_heap = var_sprint_buf(new_heap, &_s);
			}

		      if (! get_cmdline (PACKAGE " edit> ", new_heap,
					 NEW_HEAPSIZE + 1, 0, 1))
			{
			  int j = 0;

			  /* get length of new command */
			  while (new_heap[j++])
			    ;

			  if (j < 2)
			    {
			      j = 2;
			      new_heap[0] = ' ';
			      new_heap[1] = 0;
			    }

			  /* align rest of commands properly */
			  grub_memmove (cur_entry + j, cur_entry + i,
					(int) heap - ((int) cur_entry + i));

			  /* copy command to correct area */
			  grub_memmove (cur_entry, new_heap, j);

			  heap += (j - i);
			}
		    }

		  goto restart;
		}
	      if (c == 'c')
		{
		  enter_cmdline (heap, 0);
		  goto restart;
		}
#ifdef GRUB_UTIL
	      if (c == 'q')
		{
		  /* The same as ``quit''.  */
		  stop ();
		}
#endif

	      /* Check toggles here so that we don't "overwrite" existing
	       * key binding... (user should choose another key then) */
	      if (toggle_do_key(c))
	        goto restart;
	    }
	}
    }
  
  /* Attempt to boot an entry.  */
  
 boot_entry:
/* BEGIN TCG EXTENSION */
#ifdef DEBUG
    printf("tGRUB: Activating SHA1-measurements\n");
#endif
    perform_sha1 = 1;
/* END TCG EXTENSION */
  
  cls ();
  xy=0;
  gotoxy(0,0);
  setcursor (1);
  
  while (1)
    {
      int len;
/* BEGIN TCG EXTENSION */
#ifndef SHOW_SHA1
      if (config_entries)
	printf ("  Trusted GRUB now booting \'%s\'\n\n  Progress:  ",
		var_sprint_buf(get_entry (menu_entries, first_entry + entryno, 0), &len));
      else
	printf ("  Trusted GRUB now booting command-list\n\n  Progress:  ");
#endif
/* END TCG EXTENSION */

      if (! cur_entry)
	cur_entry = get_entry (config_entries, first_entry + entryno, 1);

      /* Set CURRENT_ENTRYNO for the command "savedefault".  */
      current_entryno = first_entry + entryno;
      
      if (run_script (cur_entry, heap))
	{
	  if (fallback_entryno >= 0)
	    {
	      cur_entry = NULL;
	      first_entry = 0;
	      entryno = fallback_entries[fallback_entryno];
	      fallback_entryno++;
	      if (fallback_entryno >= MAX_FALLBACK_ENTRIES
		  || fallback_entries[fallback_entryno] < 0)
		fallback_entryno = -1;
	    }
	  else
	    break;
	}
      else
	break;
    }

  show_menu = 1;
  goto restart;
}


static int
get_line_from_config (char *cmdline, int maxlen, int read_from_file)
{
  int pos = 0, literal = 0, comment = 0;
  char c;  /* since we're loading it a byte at a time! */
  
  while (1)
    {
      if (read_from_file)
	{
	  if (! grub_read (&c, 1))
	    break;
	}
      else
	{
	  if (! read_from_preset_menu (&c, 1))
	    break;
	}

      /* Skip all carriage returns.  */
      if (c == '\r')
	continue;

      /* Replace tabs with spaces.  */
      if (c == '\t')
	c = ' ';

      /* The previous is a backslash, then...  */
      if (literal)
	{
	  /* If it is a newline, replace it with a space and continue.  */
	  if (c == '\n')
	    {
	      c = ' ';
	      
	      /* Go back to overwrite a backslash.  */
	      if (pos > 0)
		pos--;
	    }
	    
	  literal = 0;
	}
	  
      /* translate characters first! */
      if (c == '\\' && ! literal)
	literal = 1;

      if (comment)
	{
	  if (c == '\n')
	    comment = 0;
	}
      else if (! pos)
	{
	  if (c == '#')
	    comment = 1;
	  else if ((c != ' ') && (c != '\n'))
	    cmdline[pos++] = c;
	}
      else
	{
	  if (c == '\n')
	    break;

	  if (pos < maxlen)
	    cmdline[pos++] = c;
	}
    }

  cmdline[pos] = 0;

  return pos;
}


/* This is the starting function in C.  */
void
cmain (void)
{
  int config_len, menu_len, num_entries;
  char *config_entries, *menu_entries;
  char *kill_buf = (char *) KILL_BUF;

  auto void reset (void);
  void reset (void)
    {
      count_lines = -1;
      config_len = 0;
      menu_len = 0;
      num_entries = 0;
      config_entries = (char *) mbi.drives_addr + mbi.drives_length;
      menu_entries = (char *) MENU_BUF;
      init_config ();
    }
  
  /* Initialize the environment for restarting Stage 2.  */
  grub_setjmp (restart_env);

  /* Init toggle triggers. */
  toggle_trigger_init();
  
  /* Initialize the kill buffer.  */
  *kill_buf = 0;

 /* Begin TCG Extension */
    // Check for a TPM
    if (check_for_tpm())
    {
	printf("Searching for TPM: ");
	tcg_check_tpm();
	// Note that tpm_present() returns a 0 if we have a TPM, otherwise 0xbb00
	if (tpm_present()) {
	    printf("False!\nDisabling Trusted GRUB functions (result = %x)\n",tpm_present());
	} else {
	    printf("Success!\nEnabling Trusted GRUB functions (result = %x)\n",tpm_present());
	}
    }
    
 /* End TCG Extension */

  /* Never return.  */
  for (;;)
    {
      int is_opened, is_preset;

      reset ();
      
      /* Here load the configuration file.  */
      
#ifdef GRUB_UTIL
      if (use_config_file)
#endif /* GRUB_UTIL */
	{
	  int i;
	  
	  /* Get a saved default entry if possible.  */
	  /* And only if not netbooting */
	  saved_entryno = 0;
	  if (config_file[0] != '('
	      || config_file[1] != 'n'
	      || config_file[2] != 'd'
	      || config_file[3] != ')')
	    {
	      char *default_file = (char *) DEFAULT_FILE_BUF;

	      *default_file = 0;
	      grub_strncat (default_file, config_file, DEFAULT_FILE_BUFLEN);
	      for (i = grub_strlen(default_file); i >= 0; i--)
		if (default_file[i] == '/')
		  {
		    i++;
		    break;
		  }
	      default_file[i] = 0;
	      grub_strncat (default_file + i, "default", DEFAULT_FILE_BUFLEN - i);
/* BEGIN TCG EXTENSION */
// Temporarily disable SHA1-measuremnt for loading of the default file
    if (perform_sha1)
    {
	old_perform_sha1_value = perform_sha1;
	perform_sha1 = 0;
    }
/* END TCG EXTENSION */
	      if (grub_open (default_file))
		{
		  char buf[10]; /* This is good enough.  */
		  char *p = buf;
		  int len;
		  
		  len = grub_read (buf, sizeof (buf));
		  if (len > 0)
		    {
		      buf[sizeof (buf) - 1] = 0;
		      safe_parse_maxint (&p, &saved_entryno);
		    }

		  grub_close ();
/* BEGIN TCG EXTENSION */
// Activate SHA1 again
    if (old_perform_sha1_value)
	perform_sha1 = old_perform_sha1_value;
/* END TCG EXTENSION */
		}
	    }
	  errnum = ERR_NONE;
	  
	  do
	    {
	      /* STATE 0:  Before any title command.
		 STATE 1:  In a title command.
		 STATE >1: In a entry after a title command.  */
	      int state = 0, prev_config_len = 0, prev_menu_len = 0;
	      char *cmdline;

	      /* Try the preset menu first. This will succeed at most once,
		 because close_preset_menu disables the preset menu.  */
	      is_opened = is_preset = open_preset_menu ();
	      if (! is_opened)
		{
		  is_opened = grub_open (config_file);
		  errnum = ERR_NONE;
		}

	      if (! is_opened)
		break;

	      /* This is necessary, because the menu must be overrided.  */
	      reset ();
	      
	      cmdline = (char *) CMDLINE_BUF;
	      while (get_line_from_config (cmdline, NEW_HEAPSIZE,
					   ! is_preset))
		{
		  struct builtin *builtin;
		  
		  /* Get the pointer to the builtin structure.  */
		  builtin = find_command (cmdline);
		  errnum = 0;
		  if (! builtin)
		    /* Unknown command. Just skip now.  */
		    continue;
		  
		  if (builtin->flags & BUILTIN_TITLE)
		    {
		      char *ptr;
		      
		      /* the command "title" is specially treated.  */
		      if (state > 1)
			{
			  /* The next title is found.  */
			  num_entries++;
			  config_entries[config_len++] = 0;
			  prev_menu_len = menu_len;
			  prev_config_len = config_len;
			}
		      else
			{
			  /* The first title is found.  */
			  menu_len = prev_menu_len;
			  config_len = prev_config_len;
			}
		      
		      /* Reset the state.  */
		      state = 1;
		      
		      /* Copy title into menu area.  */
		      ptr = skip_to (1, cmdline);
		      while ((menu_entries[menu_len++] = *(ptr++)) != 0)
			;
		    }
		  else if (! state)
		    {
		      /* Run a command found is possible.  */
		      if (builtin->flags & BUILTIN_MENU)
			{
			  char *arg = skip_to (1, cmdline);
			  (builtin->func) (arg, BUILTIN_MENU);
			  errnum = 0;
			}
		      else
			/* Ignored.  */
			continue;
		    }
		  else
		    {
		      char *ptr = cmdline;
		      
		      state++;
		      /* Copy config file data to config area.  */
		      while ((config_entries[config_len++] = *ptr++) != 0)
			;
		    }
		}
	      
	      if (state > 1)
		{
		  /* Finish the last entry.  */
		  num_entries++;
		  config_entries[config_len++] = 0;
		}
	      else
		{
		  menu_len = prev_menu_len;
		  config_len = prev_config_len;
		}
	      
	      menu_entries[menu_len++] = 0;
	      config_entries[config_len++] = 0;
	      grub_memmove (config_entries + config_len, menu_entries,
			    menu_len);
	      menu_entries = config_entries + config_len;

	      /* Make sure that all fallback entries are valid.  */
	      if (fallback_entryno >= 0)
		{
		  for (i = 0; i < MAX_FALLBACK_ENTRIES; i++)
		    {
		      if (fallback_entries[i] < 0)
			break;
		      if (fallback_entries[i] >= num_entries)
			{
			  grub_memmove (fallback_entries + i,
					fallback_entries + i + 1,
					((MAX_FALLBACK_ENTRIES - i - 1)
					 * sizeof (int)));
			  i--;
			}
		    }

		  if (fallback_entries[0] < 0)
		    fallback_entryno = -1;
		}
	      /* Check if the default entry is present. Otherwise reset
		 it to fallback if fallback is valid, or to DEFAULT_ENTRY 
		 if not.  */
	      if (default_entry >= num_entries)
		{
		  if (fallback_entryno >= 0)
		    {
		      default_entry = fallback_entries[0];
		      fallback_entryno++;
		      if (fallback_entryno >= MAX_FALLBACK_ENTRIES
			  || fallback_entries[fallback_entryno] < 0)
			fallback_entryno = -1;
		    }
		  else
		    default_entry = 0;
		}
	      
	      if (is_preset)
		close_preset_menu ();
	      else
		grub_close ();

	      /* Save history for config_file */
	      memmove(config_file_history[config_file_history_pos].filename,
		      config_file,
		      CONFIG_FILE_LEN);
	      config_file_history_prev_pos = config_file_history_pos;
	      if (++config_file_history_pos == CONFIG_FILE_HISTORY_ENTRIES)
		config_file_history_pos = 0;
	      if (config_file_history_start == config_file_history_pos &&
		  ++config_file_history_start == CONFIG_FILE_HISTORY_ENTRIES)
		config_file_history_start = 0;
	    }
	  while (is_preset);
	}

      if (! num_entries)
	{
	  /* If no acceptable config file, goto command-line, starting
	     heap from where the config entries would have been stored
	     if there were any.  */
	  enter_cmdline (config_entries, 1);
	}
      else
	{
	  /* Run menu interface.  */
	  run_menu (menu_entries, config_entries, num_entries,
		    menu_entries + menu_len, default_entry);
	}
    }
}
