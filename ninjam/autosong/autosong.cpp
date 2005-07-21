/*
    NINJAM AutoSong - AutoSong.cpp
    Copyright (C) 2005 Cockos Incorporated

    NINJAM is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    NINJAM is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with NINJAM; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/


#ifdef _WIN32
#include <windows.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>


#ifndef _WIN32
#include <sys/types.h>
#include <sys/stat.h>
  #ifndef stricmp
    #define stricmp strcasecmp
  #endif
#endif

#include "../../WDL/string.h"
#include "../../WDL/ptrlist.h"
#include "../../WDL/lineparse.h"
#include "../../WDL/vorbisencdec.h"
#include "../../WDL/wavwrite.h"

class UserChannelList;

class UserChannelValueRec
{
public:
  UserChannelValueRec() { position=0.0; length=0.0; channel=0; vdec=0; }
  double position;
  double length;
  WDL_String guidstr;
  
  UserChannelList *channel;
  VorbisDecoder *vdec;
};

class UserChannelList
{
  public:
   UserChannelList() { step_pos=0; chidx=0; }

   WDL_String user;
   int chidx;

   WDL_PtrList<UserChannelValueRec> items;


   // stepping state
   int step_pos;

};

#ifdef _WIN32

const char *realpath(const char *path, char *resolved_path) 
{
  char *p;
  if (GetFullPathName(path,1024,resolved_path,&p))
    return resolved_path;
  return NULL;
}

#define DIRCHAR '\\'
#define DIRCHAR_S "\\"


#else

#define DIRCHAR '/'
#define DIRCHAR_S "/"

#endif



int resolveFile(char *name, WDL_String *outpath, char *path)
{
  char *p=name;
  while (*p && *p == '0') p++;
  if (!*p) return 0; // empty name

  char *exts[]={".wav",".ogg"};
  WDL_String fnfind;
  int x;

  // for now, only OGG source
  for (x = 1; x < (int)(sizeof(exts)/sizeof(exts[0])); x ++)
  {
    fnfind.Set(path);
    fnfind.Append(DIRCHAR_S);
    fnfind.Append(name);
    fnfind.Append(exts[x]);

    FILE *tmp=fopen(fnfind.Get(),"rb");
    if (!tmp)
    {
      fnfind.Set(path);
      fnfind.Append(DIRCHAR_S);
      // try adding guid subdir
      char t[3]={name[0],DIRCHAR,0};
      fnfind.Append(t);

      fnfind.Append(name);
      fnfind.Append(exts[x]);
      tmp=fopen(fnfind.Get(),"rb");
    }
    if (tmp) 
    {
      fseek(tmp,0,SEEK_END);
      int l=ftell(tmp);
      fclose(tmp);
      if (l) 
      {
        char buf[4096];
        if (realpath(fnfind.Get(),buf))
        {
          outpath->Set(buf);
        }       
        return 1;
      }
    }
  }
  printf("Error resolving guid %s\n",name);
  return 0;

}

void usage()
{
   printf("Usage: \n"
          "  cliplogcvt session_directory [options]\n"
          "\n"
          "Options:\n"
          "  -skip <intervals>\n"
          "  -maxlen <intervals>\n"
          "  -concat\n"
          "  -decode\n"
          "  -decodebits 16|24\n"
          "  -insertsilence maxseconds   -- valid only with -concat -decode\n"

      );
  exit(1);
}



int main(int argc, char **argv)
{
  printf("ClipLogCvt v0.02 - Copyright (C) 2005, Cockos, Inc.\n"
         "(Converts NINJAM sessions to EDL/LOF,\n"
         " optionally writing uncompressed WAVs etc)\n\n");
  if (argc <  2 || argv[1][0] == '-')
  {
    usage();
  }
  int start_interval=1;
  int end_interval=0x40000000;


  int p;
  for (p = 2; p < argc; p++)
  {
    if (!stricmp(argv[p],"-skip"))
    {
      if (++p >= argc) usage();
      start_interval = atoi(argv[p])+1;
    }
    else if (!stricmp(argv[p],"-maxlen"))
    {
      if (++p >= argc) usage();
      end_interval = atoi(argv[p]);
    }
    else usage();
  }
  end_interval += start_interval;

  WDL_String logfn(argv[1]);
  logfn.Append(DIRCHAR_S "clipsort.log");
  FILE *logfile=fopen(logfn.Get(),"rt");
  if (!logfile)
  {
    printf("Error opening logfile\n");
    return -1;
  }

  double m_cur_bpm=-1.0;
  int m_cur_bpi=-1;
  int m_interval=0;
  
  double m_cur_position=0.0;
  double m_cur_lenblock=0.0;

  UserChannelList localrecs[32];
  WDL_PtrList<UserChannelList> curintrecs;
  

  // go through the log file
  for (;;)
  {
    char buf[4096];
    buf[0]=0;
    fgets(buf,sizeof(buf),logfile);
    if (!buf[0]) break;
    if (buf[strlen(buf)-1]=='\n') buf[strlen(buf)-1]=0;
    if (!buf[0]) continue;

    LineParser lp(0);

    int res=lp.parse(buf);

    if (res)
    {
      printf("Error parsing log line!\n");
      return -1;
    }
    else
    {
      if (lp.getnumtokens()>0)
      {
        int w=lp.gettoken_enum(0,"interval\0local\0user\0end\0");
        if (w < 0)
        {
          printf("unknown token %s\n",lp.gettoken_str(0));
          return -1;
        }
        switch (w)
        {
          case 0: // interval
            {
              if (lp.getnumtokens() != 4)
              {
                printf("interval line has wrong number of tokens\n");
                return -2;
              }

              m_cur_position+=m_cur_lenblock;

              int idx=0;
              double bpm=0.0;
              int bpi=0;
              idx=lp.gettoken_int(1);
              bpm=lp.gettoken_float(2);
              bpi=lp.gettoken_int(3);

              if ((m_cur_bpi >= 0 && m_cur_bpi != bpi) ||
                  (m_cur_bpm >= 0 && m_cur_bpm != bpm))
              {
//                printf("BPI/BPM changed from %d/%.2f to %d/%.2f\n",m_cur_bpi,m_cur_bpm,bpi,bpm);
              }

              m_cur_bpi=bpi;
              m_cur_bpm=bpm;

              m_cur_lenblock=((double)m_cur_bpi * 60000.0 / m_cur_bpm);
              m_interval++;

            }
          break;
          case 1: // local
            if (m_interval >= start_interval && m_interval < end_interval)
            {
              if (lp.getnumtokens() != 3)
              {
                printf("local line has wrong number of tokens\n");
                return -2;
              }
              UserChannelValueRec *p=new UserChannelValueRec;
              p->position=m_cur_position;
              p->length=m_cur_lenblock;
              p->guidstr.Set(lp.gettoken_str(1));
              localrecs[(lp.gettoken_int(2))&31].items.Add(p);
            }
          break;
          case 2: // user
            if (m_interval >= start_interval && m_interval < end_interval)
            {
              if (lp.getnumtokens() != 5)
              {
                printf("user line has wrong number of tokens\n");
                return -2;
              }

              char *guidtmp=lp.gettoken_str(1);
              char *username=lp.gettoken_str(2);
              int chidx=lp.gettoken_int(3);
//              char *channelname=lp.gettoken_str(4);

              //printf("Got user '%s' channel %d '%s' guid %s\n",username,chidx,channelname,guidtmp);

              UserChannelValueRec *ucvr=new UserChannelValueRec;
              ucvr->guidstr.Set(guidtmp);
              ucvr->position=m_cur_position;
              ucvr->length=m_cur_lenblock;

              int x;
              for (x = 0; x < curintrecs.GetSize(); x ++)
              {
                if (!stricmp(curintrecs.Get(x)->user.Get(),username) && curintrecs.Get(x)->chidx == chidx)
                {
                  break;
                }
              }
              if (x == curintrecs.GetSize())
              {
                // add the rec
                UserChannelList *t=new UserChannelList;
                t->user.Set(username);
                t->chidx=chidx;

                curintrecs.Add(t);
              }
              if (curintrecs.Get(x)->items.GetSize())
              {
                UserChannelValueRec *lastitem=curintrecs.Get(x)->items.Get(curintrecs.Get(x)->items.GetSize()-1); // this is for when the server sometimes groups them in the wrong interval
                double last_end=lastitem->position + lastitem->length;
                if (ucvr->position < last_end)
                {
                  ucvr->position = last_end;
                }
              }
              curintrecs.Get(x)->items.Add(ucvr);
              // add this record to it
            }

          break;
          case 3: // end
          break;
        }



      }
    }

  }
  fclose(logfile);

  printf("Done analyzing log, analzying output...\n");


  int id=1;
  int track_id=0;
  int x;
  for (x= 0; x < (int)(sizeof(localrecs)/sizeof(localrecs[0])); x ++)
  {
    localrecs[x].user.Set("local");
    if (localrecs[x].items.GetSize()) curintrecs.Add(localrecs+x);
  }


  int is_done;

  int m_not_enough_cnt=0;
  double current_position=0.0;
  do
  {
    is_done=1;
    double last_position=current_position;
    double next_position=0.0; // if items at this point, go to end of 
    double min_next_pos=100000000000.0; // if no items at this point, go to earliest of next items


    // these will store the active items this interval
    WDL_PtrList <UserChannelValueRec> m_useitems;


    for (x= 0; x < curintrecs.GetSize(); x ++)
    {
      UserChannelList *list=curintrecs.Get(x);
      if (list->step_pos < list->items.GetSize())
      {
        is_done=0;

        UserChannelValueRec *rec=list->items.Get(list->step_pos);

        if (rec->position <= current_position)
        {
          double p=rec->position + rec->length;

          if (next_position < p) next_position=p;

          rec->channel=list;

          m_useitems.Add(rec);

          list->step_pos++;
        }
        else
        {
          if (rec->position < min_next_pos)  min_next_pos=rec->position;
        }
      }        
    }

#define MIN_VOL -50.0
#define MIN_CHANNELS 2
#define MIN_INTELEN_SILENCE 2

    // decode channels, remove ones that are too silent, gate etc.
    for (x = 0; x < m_useitems.GetSize(); x ++)
    {
      UserChannelValueRec *rec=m_useitems.Get(x);


      WDL_String fn;
      FILE *fp;
      if (!resolveFile(rec->guidstr.Get(), &fn, argv[1]) || !(fp=fopen(fn.Get(),"rb")))
      {
        m_useitems.Delete(x--);
        continue;
      }
      rec->vdec = new VorbisDecoder;
      for (;;)
      {
        int l=fread(rec->vdec->DecodeGetSrcBuffer(1024),1,1024,fp);
        rec->vdec->DecodeWrote(l);
        if (!l) break;
      }
      fclose(fp);

      if (!rec->vdec->GetNumChannels() || !rec->vdec->GetSampleRate() || // invalid fmt
          rec->vdec->m_samples_used < (rec->length * rec->vdec->GetNumChannels() * rec->vdec->GetSampleRate() / 1000.0)*0.5 // insufficient samples
        ) // not 
      {
        delete rec->vdec;
        rec->vdec=0;
        m_useitems.Delete(x--);
        continue;
      }

      int l=rec->vdec->m_samples_used;
      float *p=(float *)rec->vdec->m_samples.Get();

      double mvol=pow(2.0,MIN_VOL/6.0);

      while (l-->0)
      {
        if (fabs(*p) >= mvol) break;
        p++;
      }

      if (l <= 0) // silence
      {
        delete rec->vdec;
        rec->vdec=0;
        m_useitems.Delete(x--);
        continue;
      }
    }

    if (m_useitems.GetSize() < MIN_CHANNELS)
    {
      if (m_not_enough_cnt < 65536)
        m_not_enough_cnt++;
    }
    else 
      m_not_enough_cnt=0;

    if (m_not_enough_cnt >= MIN_INTELEN_SILENCE || is_done) 
    {
      // finish any open song

      m_not_enough_cnt=65536;
    }

    if (!m_not_enough_cnt) // otherwise, open a new song
    {
    }

    // delete any decoders left
    for (x = 0; x < m_useitems.GetSize(); x ++)
    {
      delete m_useitems.Get(x)->vdec;
      m_useitems.Get(x)->vdec=0;
    }

    if (next_position < 0.001) current_position = min_next_pos; // no items, go to start of next item
    else current_position = next_position; // items, go to end of longest last item

  }
  while (!is_done);

  printf("wrote %d records, %d tracks\n",id-1,track_id);




  return 0;

}