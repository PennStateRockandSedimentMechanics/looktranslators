/********************************************************************************
 Filter to put LabView (National Instruments) data into look format
 	Data format from Matt Knuth's 24-bit xchan recorder
 	xchan Files have a fixed length footer 

    The 16 bit LabView recorder writes data of type short ints 
    The 24 bit LabView recorder writes data of type long ints 

 Input is a single file with multiplexed channels, Time and then a variable number of channels;
			e.g., time (in 1/10000 s) Ch0=vert_disp, Ch1=vert_load, 
			Ch2=horiz_disp, Ch3=horiz_load 

 Ouput is a look format rectangular table 

Chris Marone	9/94, written as sl2look.c	for superscope
S.L Karner,		February, 1997  ; modified to lv2look.c for labview
	
Modified by cjm Oct 2001, fixed problem with cfree and -t syntax 
Modified by cjm Nov 2005 to read footer and varible channel files
modified to read 24 bit data from new Linux/LabView recorder.  cjm 25.7.06
modified to write a 32 channel header, rather than 16 channel. cjm 12.2.07
modified to read 16 bit or 24 bit data. This code was called 24xchan2look and is now lv2look.  cjm 26.2.07.

cjm 6.8.08 started w/ this version after running into problems with 30.4.07 version.
 16 bit data files (e.g., p655intact100) with no footer were dying at the write stage, in 'rite'
 also, there's a byte swapping problem with integers: when I write the xlook file here, with an intel chip, and I can't read it with xlook (via code from filtersm.c)
 All fixed. I had to ax the ntohl() call in the read for 24 bit files. Not sure why. When I write an xlook file with htonl() and then read it back (all on my laptop) things work fine. But for files xferred from the logger computer, I can't use ntoh.
 I tested with 24 bit files (w/ footer), 16-bit files with footer and 16-bit files w/o footer.
e.g.,  p1000S6gr005l, p655intact100l, p1876ssd05hr05l, p1246stiffcall

cjm 10.8.08  fixed problem with writing data (table) as binary values using the htonl function, near line 417. 
tested on p655intact100l, p1876ssd05hr05l --both produce good headers and data values that are correct in xlook.

cjm 10.2.10 modify to work with new look data format as double. lv2look will now produce a data file of doubles. 
We don't need to change the way data comes in from the A2D files, only the way things get written into a look file.

cjm 20161027 modify to deal with incomplete, wrong, or missing footers

compile:
cc -o lv2look lv2look.c -lm  -I/usr/openwin/include
********************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <math.h>
#include <ctype.h>
#include <arpa/inet.h>
#include "global.h"
#define SEEK_END 2
	
char *progname;
struct  header   lookhead;			/*header for look , defined in global.h*/

int main(ac,av)

int		ac;
char	*av[];
{


void	null_col(), rite_lookfile(); 
FILE 	*header_file, *data_file, *out_file, *fopen();
static int 	i,j,k;
unsigned int n_byte, n_rec;
int	*orig_data;
int 	*int_pointer;
short   *short_p, *short_orig_data;
int	file_has_no_footer = 0;
char 	ascii_out = 'n', footer_out = 'n'; 
char	time = 'n', gain_flag[32], string[50], lv_date[9], lv_time[9];
char	data[50], outf[50];
char	xchan_data_acquisition = 'n';
char	xchan_16bit = 'n';
char	xchan_24bit = 'n';
char	force24 = 'n', force16 = 'n';
char	*buf, *buf2;
int	n_chans, n_chans_in_footer;
int	n_files;
double *f_pointer[MAX_COL]; 

	progname = av[0];

	if(ac < 2)
	{
		fprintf(stderr,"usage: %s filename [-a, -f, -m]\n",progname);
		fprintf(stderr,"\t'filename' is the base filename for the exp.\n");
		fprintf(stderr,"\toutput is to a file named *l -the letter ell is appended to 'filename'\n");
		fprintf(stderr,"\tthe -a option gives a tab-delimited ascii table of the data to stdout\n");
		fprintf(stderr,"\tthe -f option forces a 16 bit conversion. It is useful for files that aren't written properly (recorder stopped before fully writing last record?); it reduces n_rec to (n_byte-336)/(2*nchan)\n");
		fprintf(stderr,"\tthe -m option forces a 24 bit conversion. It is useful for files that aren't written properly (recorder stopped before fully writing last record?, footer is missing, etc.) You'll be prompted for info. on the file\n");
		fprintf(stderr,"\nThis program reads data (from the 24-bit or 16 bit recorders) written by LabView.\nIt expects to read time and 4 additional channels, or time and a variable number of channels plus a footer (header)\n\n");
                fprintf(stderr,"Version: 28.10.2016\n\n");
		exit(1);
	}

        fprintf(stderr,"lv2look version: 28.10.2016\n\n");

        for(i=2;i<ac;i++)
        {

		switch(*(av[i]+1))
                {
			
			case 'a':
                                ascii_out = 'y';
                                break;
			case 'f':
                                force16 = 'y';
                                break;
			case 'm':
                                force24 = 'y';
                                break;
                }
 
        }			

	footer_out = 'y';
	
/*open data file, reading data and write it as unscaled double*/

	strcpy(data, av[1]);	

	strcpy(outf, av[1]);	strcat(outf, "l");

	if( (data_file = fopen(data, "r")) == NULL )
	{
		fprintf(stderr,"\nCouldn't open data file %s \n", data);
		exit(1);
	}

	if( (out_file = fopen(outf,"w")) == NULL )
	{
		fprintf(stderr,"\nCouldn't open output file %s \n Make sure you have permission to write in this directory\n", outf);
		exit(1);
	}

/*begin by assuming that the file was written by xchan and has a footer*/
	xchan_data_acquisition = 'y';		/*assume data were recorded with xchan*/

	fseek(data_file,-336,SEEK_END);
/*note: cjm 12.2.07, footer size assumes 15 channels+time channel. We should change this to allow up to 31 channels of data + time*/
	fread(string, 32, 1, data_file);	/*filename, title*/
/*note to cjm 13.12.05: global.h assumes 20 chars for title and 13 each for name/units. Change this? */
	strlcpy(lookhead.title, string, 20);

	if(strncmp(string, av[1], 4) != 0)		//filename should be in header
		file_has_no_footer++;
						//could just quit here, but let's check a bit more, just to make sure

	fread(string, 16, 1, data_file);	/*Number of recs*/
	n_rec = atoi(string);
	fread(gain_flag, 16, 1, data_file);	/*Channel A2D gain flag */

/*fprintf(stderr,"gain flag is: %s , nrecs=%d\n", gain_flag, n_rec);*/

	fread(lv_date, 8, 1, data_file);	lv_date[8] = '\0';  /*Date String (XX/XX/XX) */
	fread(lv_time, 8, 1, data_file);	lv_time[8] = '\0';  /*Time String (XX:XX_XM or X:XX_XM_)*/



	n_chans_in_footer = n_chans=1;	/*there is always a time channel*/
	strcpy(lookhead.ch[1].name, "Time");		/*data are written to channels 0-15, but header is written from 1-16*/

/* note: cjm 12.2.07, footer size assumes 15 channels+time channel. We should change this to allow up to 31 channels of data + time */

	strcpy(lookhead.ch[1].units, "sec");		/*put in info. for Time channel*/


	for(i=0;i<15;i++)		/*set up for 15 channel, not 16 chan.,  recorder*/
/*note: cjm 12.2.07, footer size assumes 15 channels+time channel. We should change this to allow up to 31 channels of data + time*/
	{
		j=i+2;	 /*data are written to channels 0-15 at this point, but look data and header are written from 1-16*/

/*"u" indicates unused, "l" indicates a 20-volt range, "m" indicates a 10-volt range, and "s" indicates a 1-volt range.*/
		if(gain_flag[i] == 'l')		/*"gain_flag" is the 16 bit gain flag*/
		{
			lookhead.ch[j].gain = 1.0 ;	/*20 V range*/
			fread(string, 16, 1, data_file);		/*channel name*/
			strlcpy(lookhead.ch[j].name, string, 13);
			n_chans++;
		}
		else if(gain_flag[i] == 'm')
		{
			lookhead.ch[j].gain = 2.0;	/*10 V range*/
			fread(string, 16, 1, data_file);		/*channel name*/
			strlcpy(lookhead.ch[j].name, string, 13);
			n_chans++;
		}
		else if(gain_flag[i] == 's')
		{
			lookhead.ch[j].gain = 20.0;	/*1 V range*/
			fread(string, 16, 1, data_file);		/*channel name*/
			strlcpy(lookhead.ch[j].name, string, 13);
			n_chans++;
		}
	}
	n_chans_in_footer = n_chans;	/*save this in case we need it later */

	fseek(data_file,0L,SEEK_END);	
	n_byte = ftell(data_file);		/*determine the byte length, so that we can determine the number of records*/

	if(n_chans == 1)		/*if n_chans ==1, the file does not have a footer. */
	{
		file_has_no_footer++;
		strcpy(lookhead.title,av[1]); 
	}

	if(file_has_no_footer && force24 == 'n')
	{
		xchan_data_acquisition = 'n'; 	/*Use this as a flag, below, to indicate 4-chan (16 bit)  vs. xchan recorder*/
		n_chans = 5;
		strcpy(lookhead.ch[2].name, "Vert_Disp");
		strcpy(lookhead.ch[3].name, "Vert_Load");
		strcpy(lookhead.ch[4].name, "Hor_Disp");
		strcpy(lookhead.ch[5].name, "Hor_Load.");

				//assume this is a 16 bit file
		n_rec = n_byte/(2*n_chans);
	}	
	else				/*the file has a footer or it's a 24 bit file w/o a footer*/
	{
					/*determine if it is a 24 bit file or a 16 bit file*/
		if(n_byte == n_rec*2*n_chans+336 || force16 == 'y')		/*16 bit*/
		{
			xchan_16bit = 'y';
			if(force16 == 'y')	/*assume that last rec. didn't get written completely, so that file size isn't correct*/
				n_rec = floor( (n_byte-336)/(2*n_chans));
		}
		else if(n_byte == n_rec*4*n_chans+336)	/*24 bit*/
			xchan_24bit = 'y';
		else 
		{
			if(file_has_no_footer)
				fprintf(stderr,"Problem reading the data file and/or footer. \nFile: %s appears to be written by xchan but size is not correct for 16 bit or 24 bit recorder.\nLooks like the footer is missing.\n",av[1]);
			else
				fprintf(stderr,"Problem reading the data file and/or footer. \nFile appears to be written by xchan but size is not correct for 16 bit or 24 bit recorder.\nFooter size should be 336 bytes.\nReading file %s: File has %d bytes of data, and footer reports %d recs and %d chans plus time.\n If I believe n_rec and n_chan, this file should have %d bytes if it's a 16 bit file and %d bytes if it's a 24 bit file\n\n",data, n_byte, n_rec, n_chans-1, 336+(2*n_chans*n_rec), 336+(4*n_chans*n_rec));
			fprintf(stderr,"\nIf you want to write an xlook file anyway, enter the number of channels recorded (not including time), otherwise just hit return and we'll quit.\n");
			fgets(string, 3, stdin);
			n_chans = atoi(string);
			memset(string,0,sizeof(string));
			if(n_chans !=0)
			{
			 n_chans++; //to include time
			 fprintf(stderr,"Was this recorded w/ the 16 or 24-bit recorder? Enter 16 or 24\n");
			 fgets(string, 4, stdin);
			 switch(atoi(string))
			 {
				case 16:
					xchan_16bit = 'y';
					if(file_has_no_footer)			//set n_rec if we don't know it
						n_rec = floor(n_byte/(2*n_chans));
					else					//fix n_rec if it's wrong in the footer 
						n_rec = floor((n_byte-336)/(2*n_chans));
					break;
				case 24:
					xchan_24bit = 'y';
					if(file_has_no_footer)			//set n_rec if we don't know it
						n_rec = floor(n_byte/(4*n_chans));
					else					//fix n_rec if it's wrong in the footer 
						n_rec = floor((n_byte-336)/(4*n_chans));
					break;
				default:
			 		fprintf(stderr,"Problem with determining the recorder used. You have to enter 16 or 24. Sorry, can't help you otherwise.\n");
			 		exit(1);
			 }
			 if(!file_has_no_footer && n_chans == n_chans_in_footer)	//this looks like nrec is the  problem, we can use filenames and other info. in the footer 
			 {
				;		//do nothing
			 }
			 else
			 {
			  for( i=2; i<= n_chans; i++)		//start at i=2, b/c Time is in channel 1
			  {
			 	memset(string,0,sizeof(string));
			 	fprintf(stderr,"Enter the name for channel %d (12 characters)\n",i);
				fgets(string, 13, stdin);
				k=strlen(string);
				strlcpy(lookhead.ch[i].name, string, k);
			  }
			 }

			}
			else
			 exit(1);
		}
	}

/* read in data segment */

	rewind(data_file); 		/*move back to beginning of the file*/

	if(xchan_24bit == 'y') 				/*array space for data -- 4-byte words from 24 bit A2D*/
        {
                int_pointer =  (int *) calloc( (unsigned) n_byte, 1);
		fread(int_pointer, n_byte, 1, data_file);  /*6.8.08 cjm: this works. Data come from recorder in format we can use directly*/
                /*read_32(int_pointer, n_rec*n_chans, data_file);*/ /*do the high byte low byte swap, if nec. using ntohl()*/

#if 0  
(this is the old way that I did byte order swapping )

		buf =  (char *) calloc( (unsigned) n_byte, 1);
		buf2=  (char *) calloc( (unsigned) n_byte, 1);
		fread(buf, n_byte, 1, data_file);
	/*bytes are swapped for 16-bit words, and also high-low word order is swapped for 32bit words*/
		swab(buf,buf2,n_byte);		
		for(i=2;i<n_byte;i+=4)
		{
 			buf[i-2] = buf2[i];
 			buf[i-1] = buf2[i+1];
 			buf[i] = buf2[i-2];
 			buf[i+1] = buf2[i-1];
		}

		int_pointer = (int *) &buf[0];	/*this is a pointer to the start of the data, after skipping past first two words*/
#endif
	}
	else 						/*array space for 2-byte words from 16 bit A2D*/
	{
		buf =  (char *) calloc( (unsigned) n_byte, 1);
		buf2=  (char *) calloc( (unsigned) n_byte, 1);
        	/*short_orig_data =  (short *) calloc( (unsigned) n_byte, 2); cjm 6.8.08*/
        	/*fread(short_orig_data, n_byte, 1, data_file); cjm 6.8.08*/
        	fread(buf, n_byte, 1, data_file);
		swab(buf,buf2,n_byte);		
        	short_orig_data = short_p = (short *) &buf2[0]; 
	}


	fprintf(stderr,"\nReading file %s: %s-bit File has %d bytes of data, %d recs, %d chans\n",data, ((xchan_24bit == 'y') ? "24" : "16"),n_byte, n_rec, n_chans);

/* 20150922: I changed the way this works, so that it outputs the whole first record. */
	fprintf(stderr,"First recs are:\n\ttime");
	for(i=0; i<n_chans-1; ++i)
	   fprintf(stderr,", ch%d", i+2);
	fprintf(stderr,"\n\t");

	if(xchan_24bit == 'y') 				
	{
	for(i=0; i<n_chans; ++i)
	   fprintf(stderr,"%d%c ",*(int_pointer+i), i==n_chans-1 ? ' ': ',');
	}
/*fprintf(stderr,"%d, %d, %d, %d, %d\n",*int_pointer,*(int_pointer+1),*(int_pointer+2),*(int_pointer+3),*(int_pointer+4));*/
	else  				
	{
	for(i=0; i<n_chans; ++i)
	   fprintf(stderr,"%d%c ",*(short_orig_data+i), i==n_chans-1 ? ' ': ',');
	}
	fprintf(stderr,"\n");

/*
	  fprintf(stderr,"First recs are:\n\ttime, ch0, ch1, ch2, ch3\n\t%.3f, %hd, %hd, %hd, %hd\n",(double) *short_orig_data,*(short_orig_data+1),*(short_orig_data+2),*(short_orig_data+3),*(short_orig_data+4));*/



/* put data in look table ; data are written to channels 0-15 at this point, but look data and header are written from 1-16 */
	for(i=0; i < n_chans; ++i)
		f_pointer[i] = (double*) calloc(n_rec, sizeof(double) );

	if(xchan_24bit == 'y')
	{
	  for(j=0; j < n_rec; ++j)
		for(i=0; i < n_chans; ++i)
			f_pointer[i][j] = (double) *int_pointer++;
	}
	else
	{
	  for(j=0; j < n_rec; ++j)
		for(i=0; i < n_chans; ++i)
			f_pointer[i][j] = (double) *short_p++;
	}

	for(i=0; i < n_chans; ++i)			/*look data are written to channels 1-16*/
		darray[i+1] = (double *) f_pointer[i];

/*	cfree(orig_data);  --this doesn't work with PSU C, cfree() is different */
/* 25/10/01 --reason it bombs is related to use of 'short' --if orig_data is 
in terms of a short, problem doesn't occur;   But our data are shorts ---so there
must be some way to use short properly */
	/* cfree( orig_data, n_totrec, sizeof(short) ); , this doesn't work*/
/* free works if I define and use orig_data at 'int' rather than short*/

/* 30/10/01:  Looks like the problem was that I was using the pointer originally returned
from calloc as a counter-increment in a for loop.  Problem goes away if I work through the 
array with another pointer and then free the original */


	/*free( (void *)(short_orig_data) ); */

	lookhead.nchan = n_chans;
	lookhead.nrec = n_rec;

						/*put in info. for Time channel*/
	lookhead.ch[1].gain = 1;
	lookhead.ch[1].nelem = n_rec;

	for(j=2; j <= n_chans; ++j)		/*put in default units */
	{
		strcpy(lookhead.ch[j].units, "bits");
		lookhead.ch[j].nelem = n_rec;
	}

         
        for(k=lookhead.nchan+1; k < MAX_COL; ++k) /* write null columns */
                null_col(k);


					/* ascii table:  */
	if(ascii_out == 'y')
        {
                for(i=0; i < n_rec; ++i)
                {
                 printf("%d\t",i);     /*nrec is the first col*/
                 for(j=1; j <= n_chans; ++j)
			printf("%g%s",*(darray[j]+i), ((j==n_chans) ? "\n" : "\t"));
                }
        }   


					/* footer as ascii   */
	if(file_has_no_footer ==0) //footer_out == 'y' && xchan_data_acquisition == 'y' && force24 == 'n')
        {
		fprintf(stderr,"\n");
		printf("Experiment %s began at %s on %s\n", lookhead.title, lv_time, lv_date);
		printf("n_chans = %d, nrecs = %d\n",lookhead.nchan, lookhead.nrec);
		for(i=2;i<=n_chans; i++)
			printf("Channel %d (%s) was recorded at +/- %.1f volts\n", i, lookhead.ch[i].name, 10/lookhead.ch[i].gain);
	}
	else 
	{
		printf("File: %s has n_chans = %d, nrecs = %d\n",lookhead.title,lookhead.nchan, lookhead.nrec);
		for(i=2;i<=n_chans; i++)
			printf("Channel %d (%s) was recorded at unknown gain.\n", i, lookhead.ch[i].name);
	 	fprintf(stderr,"\nThis data file does not have a footer. We assumed it was a %s file, if it's not you may want to run lv2look again with the -f or -m option.\n", (xchan_24bit == 'y') ? "24 bit" : "16 bit" );
	}
		
						/*write data in look format*/
	rite_lookfile(out_file) ;


	fprintf(stderr,"\n");
	exit(0);
}

/*------------------------------------------------------------------------*/

void null_col(col)
int col ;
{
   strcpy(&(lookhead.ch[col].name[0]),"no_val") ;
   strcpy(&(lookhead.ch[col].units[0]),"no_val") ;
   strcpy(&(lookhead.ch[col].comment[0]),"none") ;
   lookhead.ch[col].nelem = 0 ;
   lookhead.ch[col].gain = 0 ;
}

/*------------------------------------------------------------------------*/

/* Note: Pay Attention to what's commented out below, see #if 0 and #endif */

#if 0
/********* rite_lookfile *****************************/
/* note that this function also exists in lv2look.c It is included here for simplicity*/
/* BUT: note that the header in xlook is called 'head' rather than 'lookhead', so be careful w/ copy/paste!*/
void rite_lookfile(FILE *dfile)
{
  int i ;

  fwrite(&(lookhead.title[0]),1,20,dfile) ;
  fwrite(&(lookhead.nrec),4,1,dfile) ;
  fwrite(&(lookhead.nchan),4,1,dfile) ;
  fwrite(&(lookhead.swp),4,1,dfile) ;
  fwrite(&(lookhead.dtime),4,1,dfile) ;
  for( i = 1; i < MAX_COL; ++i )
    {
      fwrite(&(lookhead.ch[i].name[0]),1,13,dfile) ;
      fwrite(&(lookhead.ch[i].units[0]),1,13,dfile) ;
      fwrite(&(lookhead.ch[i].gain),4,1,dfile) ;
      fwrite(&(lookhead.ch[i].comment[0]),1,50,dfile) ;
      fwrite(&(lookhead.ch[i].nelem),4,1,dfile) ;
    }

/*fprintf(stderr, "before big rite \n");*/

  for( i = 1; i < MAX_COL; ++i )
    {
      if( (strncmp(lookhead.ch[i].name,"no_val",6) != 0))
{
/*fprintf(stderr, "i=%d  chan name=%s, units are %s, nelem=%d nrec=%d\n",i, lookhead.ch[i].name,lookhead.ch[i].units, lookhead.ch[i].nelem, lookhead.nrec);*/
          fwrite(darray[i],sizeof(double),lookhead.ch[i].nelem,dfile) ;
}
    }
}
#endif

/********* rite_lookfile *****************************/
/*this is the version with network byte swapping. not sure we need that....*/
/* note that this function also exists in filterms.c It is included here just for simplicity of compile... */
/* BUT: note that the header in xlook is called 'head' rather than 'lookhead', so be careful w/ copy/paste!*/

void rite_lookfile(FILE *dfile)
{
  int i, j ;
  int write_32();
  double temp;

  fwrite(&(lookhead.title[0]),1,20,dfile) ;
  write_32(&(lookhead.nrec),1,dfile) ;
  write_32(&(lookhead.nchan),1,dfile) ;
  write_32(&(lookhead.swp),1,dfile) ;
  write_32((int *)(&(lookhead.dtime)),1,dfile) ;

  for( i = 1; i < MAX_COL; ++i ) /*for MAX_COL of 33, this will write a 32 channel header, each channel has 84 bytes of data*/
    {
      fwrite(&(lookhead.ch[i].name[0]),1,13,dfile) ;
      fwrite(&(lookhead.ch[i].units[0]),1,13,dfile) ;
      write_32((int *)(&(lookhead.ch[i].gain)),1,dfile) ;
      fwrite(&(lookhead.ch[i].comment[0]),1,50,dfile) ;
      write_32(&(lookhead.ch[i].nelem),1,dfile) ;
    }

/* fprintf(stderr, "before big rite \n");*/

  for( i = 1; i < MAX_COL; ++i )
    {
      if( (strncmp(&(lookhead.ch[i].name[0]),"no_val",6) != 0)) {
	fwrite(darray[i],sizeof(double),lookhead.ch[i].nelem,dfile) ;

/* this was used pre- Feb 2010. I don't think we can use the byte swapping in write_32 for 64 bit (double) values... 
	  for(j=0;j<lookhead.ch[i].nelem;++j)
	  {
                temp = (double) darray[i][j]; 		
		write_32((int *) &temp,1,dfile);
	  }
*/
      }
    }

}

/*********************************** read_16 *****************************/
/**
 * Read two-byte words from the data file.
 * * Reads \a count two-byte words  from \a file, and stores them at the given \a target.
 * Presumes that the file was written in big-endian (network) byte order, and
 * swaps the bytes appropriately for the current architecture.
 *
 * This function currently presumes that it is dealing with 16-bit words .
 *
 * @param target The buffer (size = sizeof(short) * count) where this function
 *               should store the results.
 * @param count The number of two-byte words to read from the file.
 * @param file The open file descriptor to read from.
 *
 * @return The number of two-byte words that were successfully read from the file.
 */
int read_16(short *target, int count, FILE *file)
{
    int num_read, i;

    num_read = fread(target, 2, count, file);
    for (i=0; i<num_read; i++)
    {
        /* integers are always written to file in big-endian byte order */
/*fprintf(stderr,"target[%d] = %X %d\n",i, target[i], target[i]);*/
        target[i] = ntohs(target[i]);
/*fprintf(stderr,"target[%d] = %X %d\n",i, target[i], target[i]);*/
    }
    return num_read;
}

/*********************************** read_32 *****************************/
/* Read four-byte words (s) from the data file.
 *
 * Reads \a count four-byte words  from \a file, and stores them at the given \a target.
 * Presumes that the file was written in big-endian (network) byte order, and
 * swaps the bytes appropriately for the current architecture.
 *
 * This function currently presumes that it is dealing with 32-bit words .
 *
 * @param target The buffer (size = sizeof(int) * count) where this function
 *               should store the results.
 * @param count The number of two-byte words to read from the file.
 * @param file The open file descriptor to read from.
 *
 * @return The number of four-byte words that were successfully read from the file.
 */
int read_32(int *target, int count, FILE *file)
{
    int num_read, i;

    num_read = fread((uint32_t *)target, 4, count, file);
    for (i=0; i<num_read; i++)
    {
        /* integers are always written to file in big-endian byte order */
/*fprintf(stderr,"target[%d] = %X\n",i, target[i]);*/
        target[i] = ntohl(target[i]); 
/*fprintf(stderr,"target[%d] = %X\n",i, target[i]);*/
    }
    return num_read;
}
/*********************************** write_32 *****************************/
/**   29.4.07 cjm --based on input from Scott Woods
* Write 32-bit values to the data file.
*
* writes 32-bit values to file
* Presumes that the file should be written in big-endian (network) byte order, and
* swaps the bytes appropriately for the current architecture.
*
* This function currently presumes that it is dealing with 32-bit values.
*
* @param target The buffer (size = sizeof(int) * count) from which this function writes
* @param count The number of values from the buffer to write to the file.
* @param file The open file descriptor to write to.
*
* @return The number of values that were successfully written to the file.
*/
int write_32(int *target, int count, FILE *file)
{
   int num_written, written, i;
   int swabbed_int;

   num_written = 0;
   for (i=0; i<count; i++)
   {
     /* convert each value to network byte order to ensure consistency across architectures. */
        /* integers are always written to file in big-endian byte order */
       swabbed_int = htonl(target[i]); 
       written = fwrite(&swabbed_int, 4, 1, file);

        if (written == 1)
           num_written++;
        else
           break;
   }
   return num_written;
}



