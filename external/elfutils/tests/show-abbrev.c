
#include <config.h>

#include <fcntl.h>
#include <inttypes.h>
#include <libdw.h>
#include <stdio.h>
#include <unistd.h>


int
main (int argc, char *argv[])
{
  int cnt;

  for (cnt = 1; cnt < argc; ++cnt)
    {
      int fd = open (argv[cnt], O_RDONLY);
      Dwarf *dbg = dwarf_begin (fd, DWARF_C_READ);
      if  (dbg == NULL)
	{
	  printf ("%s not usable: %s\n", argv[cnt], dwarf_errmsg (-1));
	  close  (fd);
	  continue;
	}

      Dwarf_Off cuoff = 0;
      Dwarf_Off old_cuoff = 0;
      size_t hsize;
      while (dwarf_nextcu (dbg, cuoff, &cuoff, &hsize, NULL, NULL, NULL) == 0)
	{
	  /* Get the DIE for the CU.  */
	  Dwarf_Die die;
 	  if (dwarf_offdie (dbg, old_cuoff + hsize, &die) == NULL)
	    /* Something went wrong.  */
	    break;

	  Dwarf_Off offset = 0;

	  while (1)
	    {
	      size_t length;
	      Dwarf_Abbrev *abbrev = dwarf_getabbrev (&die, offset, &length);
	      if (abbrev == NULL)
		/* End of the list.  */
		break;

	      unsigned tag = dwarf_getabbrevtag (abbrev);
	      if (tag == 0)
		{
		  printf ("dwarf_getabbrevtag at offset %llu returned error: %s\n",
			  (unsigned long long int) offset,
			  dwarf_errmsg (-1));
		  break;
		}

	      unsigned code = dwarf_getabbrevcode (abbrev);
	      if (code == 0)
		{
		  printf ("dwarf_getabbrevcode at offset %llu returned error: %s\n",
			  (unsigned long long int) offset,
			  dwarf_errmsg (-1));
		  break;
		}

	      int children = dwarf_abbrevhaschildren (abbrev);
	      if (children < 0)
		{
		  printf ("dwarf_abbrevhaschildren at offset %llu returned error: %s\n",
			  (unsigned long long int) offset,
			  dwarf_errmsg (-1));
		  break;
		}

	      printf ("abbrev[%llu]: code = %u, tag = %u, children = %d\n",
		      (unsigned long long int) offset, code, tag, children);

	      size_t attrcnt;
	      if (dwarf_getattrcnt (abbrev, &attrcnt) != 0)
		{
		  printf ("dwarf_getattrcnt at offset %llu returned error: %s\n",
			  (unsigned long long int) offset,
			  dwarf_errmsg (-1));
		  break;
		}

	      unsigned int attr_num;
	      unsigned int attr_form;
	      Dwarf_Off aboffset;
	      size_t j;
	      for (j = 0; j < attrcnt; ++j)
		if (dwarf_getabbrevattr (abbrev, j, &attr_num, &attr_form,
					 &aboffset))
		  printf ("dwarf_getabbrevattr for abbrev[%llu] and index %zu failed\n",
			  (unsigned long long int) offset, j);
		else
		  printf ("abbrev[%llu]: attr[%zu]: code = %u, form = %u, offset = %" PRIu64 "\n",
			  (unsigned long long int) offset, j, attr_num,
			  attr_form, (uint64_t) aboffset);

	      offset += length;
	    }

	  old_cuoff = cuoff;
	}

      if (dwarf_end (dbg) != 0)
	printf ("dwarf_end failed for %s: %s\n", argv[cnt],
		dwarf_errmsg (-1));

      close (fd);
    }

  return 0;
}
