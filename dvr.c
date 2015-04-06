#include <gst/gst.h>
#include <gtk/gtk.h>
#include <gst/video/videooverlay.h>
#include <gst/app/gstappsink.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gdk/gdkkeysyms.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAXNUM 16
#define MAXALARMS 3
#define NUMMAINBUTTONS 8
#define NUMPLAYBACKBUTTONS 12
#define ZERO_LIMIT( time) ( time < 0 ? 0: time)

void startalarmpipeline(void);
void stopplayback(void);
void startplayback(int cam, int index);
void createplaybackpipeline(void);

char *mainbuttonnames[] = {
    "View",
    "Fullscreen",
    "Recordings",
    "Settings",
    "Save",
    "Shutdown",
    "About",
    "Status"
};

char *viewmenuitemnames[] = {
    "4x4-1",
    "4x4-2",
    "Large 3x3",
    "3x3",
    "4x4"};

char *playbackbuttoniconnames[]= {
    GTK_STOCK_JUMP_TO,
    GTK_STOCK_GOTO_FIRST,
    GTK_STOCK_MEDIA_REWIND,
    GTK_STOCK_MEDIA_PREVIOUS,
    GTK_STOCK_MEDIA_NEXT,
    GTK_STOCK_MEDIA_FORWARD,
    GTK_STOCK_GOTO_LAST,
    GTK_STOCK_MEDIA_PAUSE,
    GTK_STOCK_MEDIA_PLAY,
    GTK_STOCK_FIND,
    GTK_STOCK_MEDIA_RECORD,
    GTK_STOCK_SAVE
};

char *playbackbuttonnames[] = {
    "Go To specified time",
    "First",
    "Previous hour",
    "Previous key",
    "Next key",
    "Next Hour",
    "Last",
    "Pause",
    "Play",
    "Search for activity",
    "Record clip to USB",
    "Save snapshot to USB"};


GstElement *recordpipeline[MAXNUM],
	   *v4l2src[MAXNUM],
	   *ffdeinterlace[MAXNUM] ,
	   *tee1[MAXNUM],
	   *tee2[MAXNUM],
	   *x264enc[MAXNUM],
	   *cairotextoverlay[MAXNUM],
	   *multifilesink[MAXNUM],
	   *queue1[MAXNUM],
	   *queue2[MAXNUM],
	   *queue3[MAXNUM],
	   *queue4[MAXNUM],
	   *queue5[MAXNUM],
	   *queue6[MAXNUM],
	   *queue7[MAXNUM],
	   *queue8[MAXNUM],
	   *mpegtsmux[MAXNUM],
	   *xvimagesink[MAXNUM],
	   *videorate1[MAXNUM],
	   *udpsink1[MAXNUM],
	   *videorate2[MAXNUM],
	   *videoscale2[MAXNUM],
	   *ffenc_mpeg4[MAXNUM],
	   *rtpmp4vpay[MAXNUM],
	   *udpsink2[MAXNUM],
	   *tee3[MAXNUM],
	   *ffenc_mjpeg[MAXNUM],
	   *udpsink3[MAXNUM],
	   *videorate3[MAXNUM];

GstElement *playbackpipeline,
	   *multifilesrc,
	   *mpegtsdemux,
	   *ffdech264,
	   *xvimagesinkplayback;

GstElement *imagecapturepipeline,
	   *ximagesrc,
	   *ffmpegcolorspace,
	   *jpegenc,
	   *filesink;

GstElement *alarmpipeline,
	   *playbin;

GstAppSink *appsink[MAXNUM];

GstCaps *caps1,*caps2, *caps3;
GtkWidget *mainwindow,
	  *fixedcontainer,
	  *previewwindow[MAXNUM],
	  *playbackwindow;

GtkWidget *fullscreenmenu,
	  *fullscreenmenuitems[MAXNUM],
	  *viewmenu,
	  *viewmenuitems[MAXNUM],
	  *recordmenu,
	  *recordmenuitems[MAXNUM];

GMainLoop *loop;

GtkButtonBox *mainbuttonbox;

GtkToolbar *playbackbuttonbox;

GtkSpinButton *dayspin,
	      *hourspin,
	      *minutespin,
	      *cliplengthspin;

GtkWidget *dayadjust,
	  *houradjust,
	  *minuteadjust,
	  *cliplengthadjust;

struct {
    int enabled;
    int starth;
    int startm;
    int endh;
    int endm;
} alarms[MAXNUM][MAXALARMS]={0};


struct {
    int num;
    int capwidth;
    int capheight;
    gboolean fullscreen;
    int framerate;
    int bitrate;
    char directory[PATH_MAX];
    char names[64][MAXNUM];
    int matrixwidth;
    int matrixheight;
    int buttonboxheight;
    int recorddays;
    int h264port;
    int rtpport;
    int jpegport;
    int mobilewidth;
    int mobileheight;
    int mobileframerate;
    int mobilebitrate;
    int enablertsp;
    int enablejpeg;
    int analysisframerate;
    int threshold;
    int multiplier;
    int minchange;
    int maxchange;
    int alarminterval;
    char alarmcommand[PATH_MAX];
    char alarmplaybackfile[PATH_MAX];
    int enablealarmaudio;
    char savelocation[PATH_MAX];
} conf = {
    8,
    352,
    288,
    TRUE,
    12,
    256,
    ".",
    {
	"Camera 1",
	"Camera 2",
	"Camera 3",
	"Camera 4",
	"Camera 5",
	"Camera 6",
	"Camera 7",
	"Camera 8"
    },
    3,
    3,
    35,
    7,
    5670,
    6670,
    7670,
    176,
    144,
    3,
    32000,
    1,
    1,
    3,
    30,
    8,
    1000,
    100000,
    30,
    "beep",
    "play.wav",
    TRUE,
    "/root"
};

struct {
    int cameraselection;
    int playbackmode;
    time_t lastalarmtime[MAXNUM];
} context = {
    0,
    FALSE,
    {0}
};

struct  {
    guint8 *mimage;
    guint8 *vimage;
    gboolean first;
    gboolean motion;
} md[MAXNUM] = {0};


void initconfig() {
    int i;
    for( i=0 ; i<MAXNUM ; i++){
	context.lastalarmtime[i] = time(NULL);
    }
}

GstFlowReturn
new_buffer_cb(GstAppSink *sink, gpointer data) {
    char cmd[PATH_MAX];
    time_t curtimet;
    int i=(int)data;
    int j;
    int diff;
    int change;
    struct tm *tm;

    GstSample *sample;
    GstMemory *memory;
    GstMapInfo info;
    GstBuffer *buffer = NULL;

    sample = gst_app_sink_pull_sample (appsink[i]);
    if (NULL == sample) {
	g_warning ("Error getting GStreamer sample");
	goto _return;
    }

    buffer = gst_sample_get_buffer (sample);
    if (NULL == buffer) {
	g_warning ("Error getting GStreamer buffer");
	goto _return;
    }

    memory = gst_buffer_get_memory (buffer, 0);
    if (NULL == memory) {
	g_warning ("Error getting GStreamer memory");
	goto _return;
    }

    if (gst_memory_map (memory, &info, GST_MAP_READ) == FALSE) {
	g_warning ("Error mapping GStreamer memory");
	goto _return;
    }

    if(md[i].first == FALSE) {
	md[i].mimage=g_memdup(info.data, info.size);
	md[i].vimage=g_new0(guint8, info.size);
	md[i].first=TRUE;
    }

    change=0;

    //return GST_FLOW_OK;

    for(j=0 ; j < info.size ; j++) {
	diff=(int)info.data[j] - (int)md[i].mimage[j];

	if(diff<0)
	    diff=-diff;

	if(md[i].mimage[j]<info.data[j])
	    md[i].mimage[j]++;
	else if(md[i].mimage[j]>info.data[j])
	    md[i].mimage[j]--;

	if(md[i].vimage[j]<conf.multiplier*diff)
	    md[i].vimage[j]++;
	else if(md[i].vimage[j]>conf.multiplier*diff)
	    md[i].vimage[j]--;

	if(diff>md[i].vimage[j] && diff>conf.threshold) {
	    info.data[i]=255;
	    change++;
	}
	else
	    info.data[i]=0;
    }

    if(change>conf.minchange && change<conf.maxchange) {
	md[i].motion=TRUE;
	curtimet=time(NULL);
	tm=localtime(&curtimet);

	if((curtimet-context.lastalarmtime[i]) > conf.alarminterval) {
	    for(j=0;j<MAXALARMS;j++) {
		if(alarms[i][j].enabled &&
			(((alarms[i][j].starth*60 + alarms[i][j].startm > alarms[i][j].endh*60+alarms[i][j].endm) &&
			  (tm->tm_hour*60+tm->tm_min>=alarms[i][j].starth*60+alarms[i][j].startm ||
			   tm->tm_hour*60+tm->tm_min<=alarms[i][j].endh*60+alarms[i][j].endm)) ||
			    ((alarms[i][j].starth*60+alarms[i][j].startm<alarms[i][j].endh*60+alarms[i][j].endm) &&
			     (tm->tm_hour*60+tm->tm_min >= alarms[i][j].starth*60+alarms[i][j].startm &&
			      tm->tm_hour*60+tm->tm_min<=alarms[i][j].endh*60+alarms[i][j].endm)))) {
		    sprintf(cmd,"%s %s",conf.alarmcommand,conf.names[i]);
		    g_spawn_command_line_async(cmd,NULL);
		    context.lastalarmtime[i]=curtimet;
		    if(conf.enablealarmaudio)
			startalarmpipeline();
		}
	    }
	}
    }

_return:
    if (NULL != memory) {
	gst_memory_unmap (memory, &info);
	gst_memory_unref (memory);
    }

    if (NULL != sample) {
	gst_sample_unref (sample);
    }

    return GST_FLOW_OK;
}


static gboolean
bus_callback (GstBus     *bus,
	GstMessage *message,
	gpointer    data)
{

    FILE *f;
    char *s;
    const GstStructure *struc;
    char *filename;
    int cameranum;
    char playlistfilename[PATH_MAX];
    char filedatetime[32];
    char buf[512];
    char filetitle[64]="";
    time_t curtime;
    int i,index;
    int readyear,readmonth,readday,readhour,readminute,readsecond;
    GDate *setdate,*readdate;

    struc=gst_message_get_structure(message);

    if(struc && gst_structure_has_name(struc,"GstMultiFileSink")) {
	filename=gst_structure_get_string(struc,"filename");

	if(s=strstr(filename,"Camera"))
	    cameranum=strtol(s+6,0,0);

	strcpy(playlistfilename,filename);
	strcpy((s=strchr(playlistfilename,'-'))?s:playlistfilename,".m3u8");

	if(!(f=fopen(playlistfilename,"r"))) {
	    f=fopen(playlistfilename,"w");
	    fprintf(f,"#EXTM3U\r\n");
	    fprintf(f,"#EXT-X-TARGETDURATION:10\r\n");
	}

	fclose(f);

	f=fopen(playlistfilename,"a");
	curtime=time(NULL);
	strftime(filedatetime,sizeof(filedatetime),"%Y-%m-%dT%H:%M:%SZ",localtime(&curtime));

	if(md[cameranum-1].motion) {
	    md[cameranum-1].motion=FALSE;
	    fprintf(f,"#EXT-X-MOTION\r\n");
	}

	fprintf(f,"#EXT-X-PROGRAM-DATE-TIME:%s\r\n",filedatetime);
	fprintf(f,"#EXTINF:10,%s\r\n",filetitle);
	fprintf(f,"%s\r\n",(s=strrchr(filename,'/'))?s+1:filename);
	fclose(f);

	strcpy(playlistfilename,filename);
	strcpy((s=strchr(playlistfilename,'-'))?s:playlistfilename,"-live.m3u8");
	f=fopen(playlistfilename,"w");
	fprintf(f,"#EXTM3U\r\n");
	fprintf(f,"#EXT-X-TARGETDURATION:10\r\n");
	gst_structure_get_int(struc,"index",&index);
	fprintf(f,"#EXT-X-MEDIA-SEQUENCE:%d\r\n",index-2);

	for(i=2 ; i>=0 ; i--) {
	    fprintf(f,"#EXTINF:10,%s\r\n",filetitle);
	    fprintf(f,"Camera%d-%08d.ts\r\n",cameranum,index-i);
	}

	fclose(f);
    }

    return TRUE;
}


void
setmatrix(int start,int x,int y)
{
    int i;
    int winwidth, winheight;
    if(context.playbackmode) {
	stopplayback();
	context.playbackmode=FALSE;
    }

    gdk_window_get_geometry(gtk_widget_get_window(mainwindow),
	    NULL,
	    NULL,
	    &winwidth,
	    &winheight);
    winheight-=conf.buttonboxheight;
    for(i=0;i<conf.num;i++) {
	if(i>=start && i < MIN(conf.num, start+x*y)) {
	    gtk_widget_set_size_request(previewwindow[i], winwidth/x+1, winheight/y);
	    gtk_fixed_move(GTK_FIXED(fixedcontainer),
		    previewwindow[i],
		    ((i-start)*winwidth/x)%winwidth,
		    (i-start)/x*winheight/y);
	    gtk_widget_show(previewwindow[i]);
	}
	else
	    gtk_widget_hide(previewwindow[i]);
    }
}

void
setmatrixfocus(int focus,int otherstart)
{
    int i;
    int winwidth, winheight;

    gdk_window_get_geometry(gtk_widget_get_window(mainwindow),
	    NULL,
	    NULL,
	    &winwidth,
	    &winheight);

    winheight-=conf.buttonboxheight;
    for(i=0;i<conf.num;i++) {
	if(i>=otherstart && i<MIN(conf.num, otherstart+2)) {
	    gtk_widget_set_size_request(previewwindow[i], winwidth/3+1, winheight/3);
	    gtk_fixed_move(GTK_FIXED(fixedcontainer),
		    previewwindow[i],
		    (2*winwidth/3),
		    (i-otherstart)*winheight/3);
	    gtk_widget_show(previewwindow[i]);
	}
	else if(i>=otherstart+2 && i<MIN(conf.num,otherstart+5)) {
	    gtk_widget_set_size_request(previewwindow[i], winwidth/3+1, winheight/3);
	    gtk_fixed_move(GTK_FIXED(fixedcontainer),
		    previewwindow[i],
		    (i-otherstart-2)*winwidth/3,
		    (2*winheight/3) );
	    gtk_widget_show(previewwindow[i]);
	}
	else if(i==focus) {
	    gtk_widget_set_size_request(previewwindow[i], winwidth*2/3+1, winheight*2/3);
	    gtk_fixed_move(GTK_FIXED(fixedcontainer), previewwindow[i], 0,0);
	    gtk_widget_show(previewwindow[i]);
	}
	else
	    gtk_widget_hide(previewwindow[i]);
    }
}


int
firstindex()
{
    FILE *f;
    int curindex,index,temp,i;
    char devname[128];
    char loc[PATH_MAX];

    sprintf(loc,"%s/Camera%d.m3u8",conf.directory,context.cameraselection+1);
    f=fopen(loc,"r");
    if(f) {
	while(fgets(devname,sizeof(devname),f))
	    if(sscanf(devname,"Camera%d-%d.ts",&temp,&index)==2) {
		fclose(f);
		return index;
	    }
	fclose(f);
	return -1;
    }
    return -1;
}

int
lastindex()
{
    FILE *f;
    int curindex,index,temp,i;
    char devname[128];
    char loc[PATH_MAX];
    sprintf(loc,"%s/Camera%d.m3u8",conf.directory,context.cameraselection+1);
    f=fopen(loc,"r");
    if(f) {
	while(fgets(devname,sizeof(devname),f));
	if(sscanf(devname,"Camera%d-%d.ts",&temp,&index)==2) {
	    fclose(f);
	    return index;
	}
	fclose(f);
	return -1;
    }
    return -1;
}



int
searchindex (int dayoffset,int hour,int minute)
{
    FILE *f;
    GDate *setdate,*readdate;
    int readyear,readmonth,readday,readhour,readminute,readsecond,temp,index;
    char devname[128];
    char loc[PATH_MAX];

    sprintf(loc,"%s/Camera%d.m3u8",conf.directory,context.cameraselection+1);
    f=fopen(loc,"r");
    if(f) {
	while(fgets(devname,sizeof(devname),f))
	    if(sscanf(devname,"#EXT-X-PROGRAM-DATE-TIME:%d-%d-%dT%d:%d:%dZ\r\n",
			&readyear,&readmonth,&readday,
			&readhour,&readminute,&readsecond)==6) {
		readdate=g_date_new_dmy (readday,readmonth,readyear);
		setdate=g_date_new();
		g_date_set_time_t (setdate, time (NULL));
		g_date_subtract_days(setdate,dayoffset);
		if(g_date_compare(readdate,setdate)==0 && readhour*60+readminute >= hour*60+minute)
		    while(fgets(devname,sizeof(devname),f))
			if(sscanf(devname,"Camera%d-%d.ts",&temp,&index)==2) {
			    fclose(f);
			    g_free(readdate);
			    g_free(setdate);
			    return index;
			}
		g_free(readdate);
		g_free(setdate);
	    }
	fclose(f);
    }
    return -1;
}

int
nextmotionindex(int currentindex)
{
    FILE *f;
    char devname[128];
    char loc[PATH_MAX];
    int curindex,index,temp,i;

    sprintf(loc,"%s/Camera%d.m3u8",conf.directory,context.cameraselection+1);
    f=fopen(loc,"r");
    if(f) {
	while(fgets(devname,sizeof(devname),f)) {
	    if(sscanf(devname,"Camera%d-%d.ts",&temp,&index)==2 && index==currentindex) {
		while(fgets(devname,sizeof(devname),f)) {
		    sscanf(devname,"Camera%d-%d.ts",&temp,&index);
		    if(strstr(devname,"MOTION")) {
			fclose(f);
			return index;
		    }
		}
	    }
	}

	fclose(f);
    }
    return -1;
}

void
relativeseek(int offset)
{
    int curindex,tempindex;

    g_object_get(multifilesrc,"index",&curindex,NULL);

    gst_element_set_state(playbackpipeline,GST_STATE_NULL);
    gst_object_unref (playbackpipeline);

    if(curindex+offset<(tempindex=firstindex()))
	startplayback(context.cameraselection,tempindex);
    else if(curindex+offset>(tempindex=lastindex()))
	startplayback(context.cameraselection,tempindex);
    else
	startplayback(context.cameraselection,curindex+offset);

}

void
absoluteseek(void)
{
    int tempindex;
    int dayoffset,hour,minute;

    dayoffset= gtk_spin_button_get_value_as_int(dayspin);
    hour=gtk_spin_button_get_value_as_int(hourspin);
    minute= gtk_spin_button_get_value_as_int(minutespin);

    if((tempindex=searchindex(dayoffset,hour,minute))!=-1) {
	gst_element_set_state(playbackpipeline,GST_STATE_NULL);
	gst_object_unref (playbackpipeline);
	startplayback(context.cameraselection,tempindex);
    }
}


void
firstseek()
{
    int tempindex;

    gst_element_set_state(playbackpipeline,GST_STATE_NULL);
    gst_object_unref (playbackpipeline);

    if((tempindex=firstindex())!=-1)
	startplayback(context.cameraselection,tempindex);
}

void
lastseek()
{
    int tempindex;
    gst_element_set_state(playbackpipeline,GST_STATE_NULL);
    gst_object_unref (playbackpipeline);

    if((tempindex=lastindex())!=-1)
	startplayback(context.cameraselection,tempindex);
}

void
previoushourseek()
{
    relativeseek(-361);
}

void
previouskeyseek()
{
    relativeseek(-2);
}

void
nextkeyseek()
{
    relativeseek(0);
}

void
nexthourseek()
{
    relativeseek(359);
}

void
pauseplayback()
{
    gst_element_set_state(playbackpipeline,GST_STATE_PAUSED);
}

void
resumeplayback()
{
    gst_element_set_state(playbackpipeline,GST_STATE_PLAYING);
}

void
nextmotionseek()
{
    int curindex,tempindex;

    g_object_get(multifilesrc,"index",&curindex,NULL);

    if((tempindex=nextmotionindex(curindex))!=-1) {
	gst_element_set_state(playbackpipeline,GST_STATE_NULL);
	gst_object_unref (playbackpipeline);
	startplayback(context.cameraselection,tempindex);
    }
}

void
startimagecapturepipeline()
{
    static int cntr=0;
    char uri[PATH_MAX];

    sprintf(uri,"%s/%s-%d.jpg",conf.savelocation,conf.names[context.cameraselection],++cntr);
    g_object_set(G_OBJECT(filesink),"location",uri,NULL);
    gst_element_set_state (imagecapturepipeline, GST_STATE_PLAYING);
}

void
savemovieclip()
{
    char buf[512];
    int i;
    static int cntr=0;
    FILE *rf,*wf;
    int curindex,readreturn;
    int cliplength;
    char loc[PATH_MAX];

    g_object_get(multifilesrc,"index",&curindex,NULL);
    sprintf(loc,"%s/%s-%d.ts",conf.savelocation,conf.names[context.cameraselection],++cntr);
    wf=fopen(loc,"wb");

    if(wf) {
	cliplength= gtk_spin_button_get_value_as_int(cliplengthspin);

	for(i=0; i<cliplength*6; i++) {
	    sprintf(loc,"%s/Camera%d-%08d.ts",conf.directory,context.cameraselection+1,curindex+i);
	    rf=fopen(loc,"rb");
	    if(rf) {
		while(readreturn=fread(buf,1,512,rf))
		    fwrite(buf,1,readreturn,wf);
		fclose(rf);
	    }
	}
	fclose(wf);
    }
}

void
startplayback(int cam,int index)
{
    int i;
    char loc[PATH_MAX];

    context.playbackmode=TRUE;
    createplaybackpipeline();
    sprintf(loc,"%s/Camera%d-%%08d.ts",conf.directory,cam+1);
    g_object_set(G_OBJECT(multifilesrc),"location",loc,NULL);
    g_object_set(G_OBJECT(multifilesrc),"index",index,NULL);

    gtk_widget_show(playbackwindow);
    gtk_widget_show(playbackbuttonbox);
    gtk_widget_hide(mainbuttonbox);

    gdk_window_raise(gtk_widget_get_window(playbackwindow));

    gst_video_overlay_set_window_handle(xvimagesinkplayback,
	    GDK_WINDOW_XWINDOW(gtk_widget_get_window(playbackwindow)));
    gst_element_set_state (playbackpipeline, GST_STATE_PLAYING);
    gst_video_overlay_set_window_handle(xvimagesinkplayback,
	    GDK_WINDOW_XWINDOW(gtk_widget_get_window(playbackwindow)));

    /*
       if(context.initialplayback){
       gtk_window_move(mainwindow,50,50);
       gtk_window_move(mainwindow,0,0);
       context.initialplayback=FALSE;
       }
       */
}

void
stopplayback(void)
{
    context.playbackmode=FALSE;

    gst_element_set_state(playbackpipeline,GST_STATE_NULL);
    gtk_widget_hide(playbackwindow);
    gtk_widget_hide(playbackbuttonbox);
    gtk_widget_show(mainbuttonbox);
    gst_object_unref (playbackpipeline);
}


void
destroy_cb(GtkWidget * widget, gpointer data)
{
    GMainLoop *loop = (GMainLoop*) data;
    g_main_loop_quit(loop);
}

static gboolean
key_press_event_cb(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    int i;
    GstState playstate;

    switch(event->keyval)
    {
	case GDK_KEY_Escape:
	    gtk_widget_destroy(widget);
	    break;
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	    setmatrix(event->keyval-'1',1,1);
	    context.cameraselection=event->keyval-'1';
	    if(context.playbackmode) {
		stopplayback();
		startplayback(context.cameraselection,0);
	    }
	    break;
	case GDK_KEY_F1:
	    setmatrix(0,2,2);
	    break;
	case GDK_KEY_F2:
	    setmatrix(4,2,2);
	    break;
	case GDK_KEY_F3:
	    setmatrixfocus(0,1);
	    break;
	case GDK_KEY_F4:
	    setmatrix(0,3,3);
	    break;
	case GDK_KEY_F5:
	    setmatrix(0,4,4);
	    break;
	case GDK_KEY_Left:
	    if(context.playbackmode)
		previouskeyseek();
	    break;
	case GDK_KEY_Right:
	    if(context.playbackmode)
		nextkeyseek();
	    break;
	case GDK_KEY_Up:
	    if(context.playbackmode)
		previoushourseek();
	    break;
	case GDK_KEY_Down:
	    if(context.playbackmode)
		nexthourseek();
	    break;
	case GDK_KEY_Home:
	    if(context.playbackmode)
		firstseek();
	    break;
	case GDK_KEY_End:
	    if(context.playbackmode)
		lastseek();
	    break;
	case GDK_KEY_Insert:
	    if(context.playbackmode) {
		gst_element_get_state(playbackpipeline,&playstate,
			NULL,
			GST_CLOCK_TIME_NONE);
		if(playstate==GST_STATE_PAUSED)
		    resumeplayback();
		else
		    pauseplayback();
	    }
	    break;
	case ' ':
	    if(context.playbackmode)
		stopplayback();
	    else
		startplayback(context.cameraselection,0);
	    break;

    }
    return TRUE;
}

static gint
main_button_cb( gpointer i,
	GdkEvent *event )
{
    int index=(gint)i;

    if (event->type == GDK_BUTTON_PRESS) {
	GdkEventButton *bevent = (GdkEventButton *) event;

	switch(index) {
	    case 0:
		gtk_menu_popup (GTK_MENU (viewmenu), NULL, NULL,
			NULL, NULL,bevent->button, bevent->time);
		break;
	    case 1:
		gtk_menu_popup (GTK_MENU (fullscreenmenu), NULL, NULL,
			NULL, NULL,bevent->button, bevent->time);
		break;
	    case 2:
		gtk_menu_popup (GTK_MENU (recordmenu), NULL, NULL,
			NULL, NULL,bevent->button, bevent->time);
		break;
	}
	return FALSE;
    }
    return FALSE;
}

static void
playback_button_cb(gpointer data)
{
    int index=(gint)data;

    g_print("test %d\n",index);
    return FALSE;
}


static gint
back_button_cb(gpointer i,GdkEvent *event)
{
    int index=(gint)i;
    if (event->type == GDK_BUTTON_PRESS) {
	stopplayback();
    }
    return FALSE;
}

static void
fullscreen_menu_item_cb( gpointer data)
{
    int i=(gint) data;
    setmatrix(i,1,1);
    context.cameraselection=i;
}

static void
view_menu_item_cb( gpointer data)
{
    int i=(gint) data;
    setmatrix(i,1,1);
    context.cameraselection=i;
    switch(i){
	case 0:
	    setmatrix(0,2,2);
	    break;
	case 1:
	    setmatrix(4,2,2);
	    break;
	case 2:
	    setmatrixfocus(0,1);
	    break;
	case 3:
	    setmatrix(0,3,3);
	    break;
	case 4:
	    setmatrix(0,4,4);
	    break;
    }
}

static void
record_menu_item_cb( gpointer data)
{
    int i=(gint) data;
    if(context.playbackmode) stopplayback();
    context.cameraselection=i;
    context.playbackmode=TRUE;
    startplayback(context.cameraselection,i);
}

void createui() {
    int i;
    int winwidth,winheight;
    GtkWidget *mainbutton[NUMMAINBUTTONS];
    GtkWidget *playbackbutton[NUMPLAYBACKBUTTONS];

    GdkColor color;
    GtkWidget *daylabel,*hourlabel,*minutelabel,*cliplengthlabel,*backbutton;
    typedef void (*playfunc) (void);

    playfunc playback_button_cb[] = {
	absoluteseek,
	firstseek,
	previoushourseek,
	previouskeyseek,
	nextkeyseek,
	nexthourseek,
	lastseek,
	pauseplayback,
	resumeplayback,
	nextmotionseek,
	savemovieclip,
	startimagecapturepipeline
    };

    mainwindow = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gdk_color_parse ("blue3", &color);
    gtk_widget_override_background_color(mainwindow, GTK_STATE_NORMAL, &color);

    gtk_widget_set_events(mainwindow, GDK_KEY_PRESS_MASK);
    g_signal_connect(G_OBJECT(mainwindow), "key-press-event", G_CALLBACK(key_press_event_cb), mainwindow);
    g_signal_connect(G_OBJECT(mainwindow), "destroy", G_CALLBACK(destroy_cb), loop);

    gtk_window_set_default_size(GTK_WINDOW(gtk_widget_get_window(mainwindow)),800,600);
    if(conf.fullscreen)
	gtk_window_fullscreen (gtk_widget_get_window(mainwindow));

    gdk_window_get_geometry(gtk_widget_get_window(mainwindow),
	    NULL,
	    NULL,
	    &winwidth,
	    &winheight);

    fixedcontainer=gtk_fixed_new();
    gtk_container_add (GTK_CONTAINER (mainwindow), fixedcontainer);
    for(i=0; i<conf.num; i++) {
	previewwindow[i]=gtk_drawing_area_new();
	gtk_widget_set_events(previewwindow[i], GDK_KEY_PRESS_MASK);
	g_signal_connect(G_OBJECT(previewwindow[i]), "key-press-event",
		G_CALLBACK(key_press_event_cb), previewwindow[i]);
	gtk_fixed_put(GTK_FIXED(fixedcontainer), previewwindow[i], 0, 0);
    }

    playbackwindow=gtk_drawing_area_new();
    gtk_widget_set_size_request(playbackwindow, winwidth,
	    winheight-conf.buttonboxheight);
    gtk_widget_set_events(playbackwindow, GDK_KEY_PRESS_MASK);
    g_signal_connect(G_OBJECT(playbackwindow), "key-press-event",
	    G_CALLBACK(key_press_event_cb), playbackwindow);
    gtk_fixed_put(GTK_FIXED(fixedcontainer), playbackwindow, 0, 0);

    setmatrix(0,conf.matrixwidth,conf.matrixheight);

    mainbuttonbox=gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    playbackbuttonbox=gtk_toolbar_new();

    gtk_widget_set_size_request(mainbuttonbox, winwidth, conf.buttonboxheight);
    gtk_button_box_set_layout(mainbuttonbox, GTK_BUTTONBOX_START);
    gtk_fixed_put(GTK_FIXED(fixedcontainer), mainbuttonbox, 0, winheight-conf.buttonboxheight);

    gtk_button_box_set_child_size (GTK_BUTTON_BOX (mainbuttonbox),
	    winwidth/(sizeof(mainbutton)/sizeof(GtkWidget *)),
	    conf.buttonboxheight);

    gtk_widget_set_size_request(playbackbuttonbox, winwidth, conf.buttonboxheight);
    gtk_toolbar_set_style (playbackbuttonbox,GTK_TOOLBAR_ICONS);
    //gtk_toolbar_set_tooltips(playbackbuttonbox,TRUE);

    gtk_fixed_put(GTK_FIXED(fixedcontainer),playbackbuttonbox, 0, winheight-conf.buttonboxheight);

    for(i=0;i<NUMMAINBUTTONS;i++) {
	mainbutton[i]=gtk_button_new_with_label (mainbuttonnames[i]);
	gtk_container_add(mainbuttonbox,mainbutton[i]);
	g_signal_connect_swapped (mainbutton[i], "event",
	G_CALLBACK(main_button_cb), (gpointer)i);
    }

    dayadjust=gtk_adjustment_new (0.0, 0.0, conf.recorddays, 1.0,2.0, 0.0);
    houradjust=gtk_adjustment_new (10.0, 0.0, 23.0, 1.0,6.0, 0.0);
    minuteadjust=gtk_adjustment_new (10.0, 0.0, 59.0, 1.0,15.0, 0.0);
    cliplengthadjust=gtk_adjustment_new (15.0, 0.0, 300.0, 1.0,15.0, 0.0);

    dayspin=gtk_spin_button_new( dayadjust,0.3,0);
    hourspin=gtk_spin_button_new( houradjust,0.3,0);
    minutespin=gtk_spin_button_new( minuteadjust,0.3,0);
    cliplengthspin=gtk_spin_button_new( cliplengthadjust,0.3,0);

    daylabel=gtk_label_new("Days back");
    hourlabel=gtk_label_new(" at ");
    minutelabel=gtk_label_new(":");
    cliplengthlabel=gtk_label_new("Minutes");

    gtk_toolbar_append_widget (playbackbuttonbox,dayspin,"Choose day","Choose day");
    gtk_toolbar_append_widget (playbackbuttonbox,daylabel,"","");

    gtk_toolbar_append_widget (playbackbuttonbox,hourlabel,"","");
    gtk_toolbar_append_widget (playbackbuttonbox,hourspin,"Choose hour","Choose hour");

    gtk_toolbar_append_widget (playbackbuttonbox,minutelabel,"","");
    gtk_toolbar_append_widget (playbackbuttonbox,minutespin,"Choose minute","Choose minute");

    playbackbutton[0]=gtk_tool_button_new_from_stock (playbackbuttonnames[0]);
    gtk_toolbar_insert_stock (playbackbuttonbox,
	    playbackbuttoniconnames[0],
	    playbackbuttonnames[0],
	    playbackbuttonnames[0],
	    playback_button_cb[0],
	    i,
	    6);
    //gtk_toolbar_insert(playbackbuttonbox,playbackbutton[0],6);

    //gtk_signal_connect_object (GTK_OBJECT (playbackbutton[0]), "clicked",GTK_SIGNAL_FUNC (playback_button_cb),(gpointer)0);
    gtk_toolbar_append_space (playbackbuttonbox);

    for(i=1;i<NUMPLAYBACKBUTTONS-2;i++) {
	playbackbutton[i]=gtk_tool_button_new_from_stock (playbackbuttonnames[i]);

	gtk_toolbar_insert_stock (playbackbuttonbox,
		playbackbuttoniconnames[i],
		playbackbuttonnames[i],
		playbackbuttonnames[i],
		playback_button_cb[i],
		i,
		i+7);
	//gtk_toolbar_insert(playbackbuttonbox,playbackbutton[i],7+i);

	//gtk_signal_connect_object (GTK_OBJECT (playbackbutton[i]), "clicked",GTK_SIGNAL_FUNC (playback_button_cb),(gpointer)i);

    }

    gtk_toolbar_append_space (playbackbuttonbox);
    gtk_toolbar_append_widget (playbackbuttonbox,
	    cliplengthspin,
	    "Length of clip in minutes",
	    "Length of clip in minutes");
    gtk_toolbar_append_widget (playbackbuttonbox,cliplengthlabel,"","");
    gtk_toolbar_insert_stock (
	    playbackbuttonbox,
	    playbackbuttoniconnames[NUMPLAYBACKBUTTONS-2],
	    playbackbuttonnames[NUMPLAYBACKBUTTONS-2],
	    playbackbuttonnames[NUMPLAYBACKBUTTONS-2],
	    playback_button_cb[NUMPLAYBACKBUTTONS-2],
	    NUMPLAYBACKBUTTONS-2,
	    NUMPLAYBACKBUTTONS+9);
    gtk_toolbar_insert_stock (playbackbuttonbox,
	    playbackbuttoniconnames[NUMPLAYBACKBUTTONS-1],
	    playbackbuttonnames[NUMPLAYBACKBUTTONS-1],
	    playbackbuttonnames[NUMPLAYBACKBUTTONS-1],
	    playback_button_cb[NUMPLAYBACKBUTTONS-1],
	    NUMPLAYBACKBUTTONS-1,
	    NUMPLAYBACKBUTTONS+10);

    gtk_toolbar_append_space (playbackbuttonbox);
    backbutton=gtk_button_new_from_stock (GTK_STOCK_GO_BACK);
    gtk_toolbar_append_widget (playbackbuttonbox,backbutton,"","");
    g_signal_connect_swapped (backbutton, "event", G_CALLBACK(back_button_cb), (gpointer)NULL);

    gtk_widget_show_all(mainwindow);
    gtk_widget_hide(playbackbuttonbox);
    gtk_widget_hide(playbackwindow);

    for(i=0;i<conf.num;i++) {
	gst_video_overlay_set_window_handle(xvimagesink[i], gtk_widget_get_window(previewwindow[i]));
    }

    fullscreenmenu=gtk_menu_new();
    viewmenu=gtk_menu_new();
    recordmenu=gtk_menu_new();

    for(i=0;i<conf.num;i++) {
	char *markup;

	fullscreenmenuitems[i]=gtk_menu_item_new_with_label(conf.names[i]);
	gtk_menu_append (GTK_MENU (fullscreenmenu), fullscreenmenuitems[i]);
	markup = g_markup_printf_escaped ("<span size=\"large\">%s</span>", conf.names[i]);
	gtk_label_set_markup (GTK_LABEL (gtk_bin_get_child(GTK_BIN(fullscreenmenuitems[i]))), markup);
	g_free (markup);

	gtk_widget_show (fullscreenmenuitems[i]);
	g_signal_connect_swapped (fullscreenmenuitems[i],
		"activate", G_CALLBACK(fullscreen_menu_item_cb),
		(gpointer) i);

	recordmenuitems[i]=gtk_menu_item_new_with_label(conf.names[i]);
	gtk_menu_append (GTK_MENU (recordmenu), recordmenuitems[i]);
	markup = g_markup_printf_escaped ("<span size=\"large\">%s</span>", conf.names[i]);
	gtk_label_set_markup (GTK_LABEL (gtk_bin_get_child(GTK_BIN(recordmenuitems[i]))), markup);
	g_free (markup);

	gtk_widget_show (recordmenuitems[i]);
	g_signal_connect_swapped (recordmenuitems[i],
		"activate",
		G_CALLBACK(record_menu_item_cb),
		(gpointer) i);
    }

    for(i=0; i<sizeof(viewmenuitemnames)/sizeof(char *); i++) {
	char *markup;

	viewmenuitems[i]=gtk_menu_item_new_with_label(viewmenuitemnames[i]);
	gtk_menu_append (GTK_MENU (viewmenu), viewmenuitems[i]);
	markup = g_markup_printf_escaped ("<span size=\"large\">%s</span>", viewmenuitemnames[i]);
	gtk_label_set_markup (GTK_LABEL (gtk_bin_get_child(GTK_BIN(viewmenuitems[i]))), markup);
	g_free (markup);
	gtk_widget_show (viewmenuitems[i]);
	g_signal_connect_swapped (viewmenuitems[i],
		"activate",
		G_CALLBACK(view_menu_item_cb),
		(gpointer) i);
    }
}

void createrecordpipeline(int i){
    char devname[128];
    char loc[PATH_MAX];
    FILE *f;
    int temp,index;

    GstAppSinkCallbacks appcb = {
	NULL,
	NULL,
	new_buffer_cb,
	NULL
    };

    sprintf(devname,"recordpipeline%d",i);
    recordpipeline[i] = gst_pipeline_new (devname);

    sprintf(devname,"v4l2src%d",i);
    v4l2src[i] = gst_element_factory_make ("v4l2src", devname);

    sprintf(devname,"/dev/video%d",i);
    g_object_set(G_OBJECT(v4l2src[i]),"device",devname,NULL);

    sprintf(devname,"ffdeinterlace%d",i);
    ffdeinterlace[i] = gst_element_factory_make ("ffdeinterlace", devname);

    sprintf(devname,"videorate1%d",i);
    videorate1[i] = gst_element_factory_make ("videorate", devname);

    sprintf(devname,"cairotextoverlay%d",i);
    cairotextoverlay[i] = gst_element_factory_make ("cairotextoverlay", devname);

    g_object_set(G_OBJECT(cairotextoverlay[i]),"font-desc","Sans Normal 12",NULL);
    g_object_set(G_OBJECT(cairotextoverlay[i]),"halign","left",NULL);

    sprintf(devname,"tee1%d",i);
    tee1[i] = gst_element_factory_make ("tee", devname);

    sprintf(devname,"queue1%d",i);
    queue1[i] = gst_element_factory_make("queue", devname);

    sprintf(devname,"x264enc%d",i);
    x264enc[i] = gst_element_factory_make("x264enc", devname);

    g_object_set(G_OBJECT(x264enc[i]),"bitrate",conf.bitrate,NULL);
    g_object_set(G_OBJECT(x264enc[i]),"b-adapt",FALSE,NULL);
    g_object_set(G_OBJECT(x264enc[i]),"bframes",0,NULL);
    g_object_set(G_OBJECT(x264enc[i]),"key-int-max",15,NULL);

    sprintf(devname,"mpegtsmux%d",i);
    mpegtsmux[i] = gst_element_factory_make("mpegtsmux", devname);
    sprintf(devname,"tee2%d",i);
    tee2[i] = gst_element_factory_make ("tee", devname);
    sprintf(devname,"queue3%d",i);
    queue3[i] = gst_element_factory_make ("queue", devname);
    sprintf(devname,"queue4%d",i);
    queue4[i] = gst_element_factory_make ("queue", devname);

    sprintf(devname,"multifilesink%d",i);
    multifilesink[i] = gst_element_factory_make("multifilesink", devname);
    sprintf(loc,"%s/Camera%d-%%08d.ts",conf.directory,i+1);

    g_object_set(G_OBJECT(multifilesink[i]),"location",loc,NULL);
    g_object_set(G_OBJECT(multifilesink[i]),"next-file",2,NULL);
    g_object_set(G_OBJECT(multifilesink[i]),"post-messages",TRUE,NULL);

    sprintf(loc,"%s/Camera%d.m3u8",conf.directory,i+1);
    f=fopen(loc,"r");

    if(f) {
	while(fgets(devname,sizeof(devname),f));
	if(sscanf(devname,"Camera%d-%d.ts",&temp,&index)==2)
	    g_object_set(G_OBJECT(multifilesink[i]),"index",index+1,NULL);
	fclose(f);
    }

    sprintf(devname,"udpsink1%d",i);
    udpsink1[i] = gst_element_factory_make ("udpsink", devname);
    g_object_set(G_OBJECT(udpsink1[i]),"host","127.0.0.1",NULL);
    g_object_set(G_OBJECT(udpsink1[i]),"port",conf.h264port+i,NULL);

    sprintf(devname,"queue2%d",i);
    queue2[i] = gst_element_factory_make("queue", devname);
    sprintf(devname,"xvimagesink%d",i);
    xvimagesink[i] = gst_element_factory_make("xvimagesink", devname);

    sprintf(devname,"queue5%d",i);
    queue5[i] = gst_element_factory_make("queue", devname);

    sprintf(devname,"videoscale2%d",i);
    videoscale2[i] = gst_element_factory_make("videoscale", devname);

    sprintf(devname,"videorate2%d",i);
    videorate2[i] = gst_element_factory_make("videorate", devname);

    sprintf(devname,"tee3%d",i);
    tee3[i] = gst_element_factory_make ("tee", devname);

    sprintf(devname,"queue6%d",i);
    queue6[i] = gst_element_factory_make ("queue", devname);

    sprintf(devname,"queue7%d",i);
    queue7[i] = gst_element_factory_make ("queue", devname);

    sprintf(devname,"ffenc_mpeg4%d",i);
    ffenc_mpeg4[i] = gst_element_factory_make("ffenc_mpeg4", devname);
    g_object_set(G_OBJECT(ffenc_mpeg4[i]),"max-key-interval",1,NULL);
    g_object_set(G_OBJECT(ffenc_mpeg4[i]),"bitrate",conf.mobilebitrate,NULL);

    sprintf(devname,"rtpmp4vpay%d",i);
    rtpmp4vpay[i] = gst_element_factory_make("rtpmp4vpay", devname);
    g_object_set(G_OBJECT(rtpmp4vpay[i]),"send-config",TRUE,NULL);

    sprintf(devname,"udpsink2%d",i);
    udpsink2[i] = gst_element_factory_make("udpsink", devname);

    g_object_set(G_OBJECT(udpsink2[i]),"host","127.0.0.1",NULL);
    g_object_set(G_OBJECT(udpsink2[i]),"port",conf.rtpport+i*2,NULL);

    sprintf(devname,"ffenc_mjpeg%d",i);
    ffenc_mjpeg[i] = gst_element_factory_make("ffenc_mjpeg", devname);
    g_object_set(G_OBJECT(ffenc_mpeg4[i]),"bitrate",conf.mobilebitrate,NULL);

    sprintf(devname,"udpsink3%d",i);
    udpsink3[i] = gst_element_factory_make("udpsink", devname);

    g_object_set(G_OBJECT(udpsink3[i]),"host","127.0.0.1",NULL);
    g_object_set(G_OBJECT(udpsink3[i]),"port",conf.jpegport+i,NULL);

    sprintf(devname,"queue8%d",i);
    queue8[i] = gst_element_factory_make ("queue", devname);

    sprintf(devname,"videorate3%d",i);
    videorate3[i] = gst_element_factory_make ("videorate", devname);

    sprintf(devname,"appsink%d",i);
    appsink[i] = gst_element_factory_make("appsink", devname);

    gst_bin_add (GST_BIN (recordpipeline[i]), v4l2src[i]);
    gst_bin_add (GST_BIN (recordpipeline[i]), ffdeinterlace[i]);
    gst_bin_add (GST_BIN (recordpipeline[i]), videorate1[i]);
    gst_bin_add (GST_BIN (recordpipeline[i]), cairotextoverlay[i]);
    gst_bin_add (GST_BIN (recordpipeline[i]), tee1[i]);
    gst_bin_add (GST_BIN (recordpipeline[i]), tee2[i]);
    gst_bin_add (GST_BIN (recordpipeline[i]), queue1[i]);
    gst_bin_add (GST_BIN (recordpipeline[i]), x264enc[i]);
    gst_bin_add (GST_BIN (recordpipeline[i]), mpegtsmux[i]);
    gst_bin_add (GST_BIN (recordpipeline[i]), multifilesink[i]);
    gst_bin_add (GST_BIN (recordpipeline[i]), queue2[i]);
    gst_bin_add (GST_BIN (recordpipeline[i]), xvimagesink[i]);
    gst_bin_add (GST_BIN (recordpipeline[i]), queue3[i]);
    gst_bin_add (GST_BIN (recordpipeline[i]), queue4[i]);
    gst_bin_add (GST_BIN (recordpipeline[i]), udpsink1[i]);

    if(conf.enablertsp || conf.enablejpeg) {
	gst_bin_add (GST_BIN (recordpipeline[i]), queue5[i]);
	gst_bin_add (GST_BIN (recordpipeline[i]), videoscale2[i]);
	gst_bin_add (GST_BIN (recordpipeline[i]), videorate2[i]);
	gst_bin_add (GST_BIN (recordpipeline[i]), tee3[i]);

    }

    if(conf.enablertsp) {
	gst_bin_add (GST_BIN (recordpipeline[i]), queue6[i]);
	gst_bin_add (GST_BIN (recordpipeline[i]), ffenc_mpeg4[i]);
	gst_bin_add (GST_BIN (recordpipeline[i]), rtpmp4vpay[i]);
	gst_bin_add (GST_BIN (recordpipeline[i]), udpsink2[i]);
    }

    if(conf.enablejpeg) {
	gst_bin_add (GST_BIN (recordpipeline[i]), queue7[i]);
	gst_bin_add (GST_BIN (recordpipeline[i]), ffenc_mjpeg[i]);
	gst_bin_add (GST_BIN (recordpipeline[i]), udpsink3[i]);
    }

    gst_bin_add (GST_BIN (recordpipeline[i]), queue8[i]);
    gst_bin_add (GST_BIN (recordpipeline[i]), videorate3[i]);
    gst_bin_add (GST_BIN (recordpipeline[i]), appsink[i]);

    gst_element_link(v4l2src[i], videorate1[i]);
    gst_element_link_filtered(videorate1[i], ffdeinterlace[i],caps1);
    gst_element_link(ffdeinterlace[i], cairotextoverlay[i]);
    gst_element_link(cairotextoverlay[i],tee1[i]);
    gst_element_link(tee1[i], queue1[i]);
    gst_element_link(queue1[i],x264enc[i]);
    gst_element_link(x264enc[i],mpegtsmux[i]);
    gst_element_link(mpegtsmux[i],tee2[i]);
    gst_element_link(tee2[i],queue3[i]);
    gst_element_link(queue3[i],multifilesink[i]);
    gst_element_link(tee2[i],queue4[i]);
    gst_element_link(queue4[i],udpsink1[i]);
    gst_element_link(tee1[i], queue2[i]);
    gst_element_link(queue2[i],xvimagesink[i]);

    if(conf.enablertsp || conf.enablejpeg) {
	gst_element_link(tee1[i], queue5[i]);
	gst_element_link(queue5[i],videoscale2[i]);
	gst_element_link(videoscale2[i],videorate2[i]);
	gst_element_link_filtered(videorate2[i],tee3[i],caps2);
    }

    if(conf.enablertsp) {
	gst_element_link(tee3[i],queue6[i]);
	gst_element_link(queue6[i],ffenc_mpeg4[i]);
	gst_element_link(ffenc_mpeg4[i],rtpmp4vpay[i]);
	gst_element_link(rtpmp4vpay[i],udpsink2[i]);
    }

    if(conf.enablejpeg) {
	gst_element_link(tee3[i],queue7[i]);
	gst_element_link(queue7[i],ffenc_mjpeg[i]);
	gst_element_link(ffenc_mjpeg[i],udpsink3[i]);
    }

    gst_element_link(tee1[i], queue8[i]);
    gst_element_link(queue8[i],videorate3[i]);
    gst_element_link_filtered(videorate3[i],appsink[i],caps3);

    gst_app_sink_set_callbacks(appsink[i],&appcb,(gpointer)i,NULL);

    gst_bus_add_watch(gst_pipeline_get_bus (GST_PIPELINE (recordpipeline[i])),bus_callback,NULL);

}


static void
mpegtsdemux_pad_added_cb (GstElement *element,
	GstPad     *pad,
	gpointer    data)
{
    gchar *name;

    name = gst_pad_get_name (pad);
    gst_element_link_pads (mpegtsdemux, name, ffdech264, "sink");
    g_free (name);
}


void
createimagecapturepipeline(void)
{
    char uri[PATH_MAX];
    int winwidth;int winheight;
    static int cntr=0;

    gdk_window_get_geometry(gtk_widget_get_window(mainwindow),NULL,NULL,&winwidth,&winheight);

    imagecapturepipeline = gst_pipeline_new ("imagecapturepipeline");
    ximagesrc = gst_element_factory_make ("ximagesrc", "ximagesrc");
    g_object_set(G_OBJECT(ximagesrc),"num-buffers",1,NULL);
    g_object_set(G_OBJECT(ximagesrc),"endy",winheight-conf.buttonboxheight,NULL);

    ffmpegcolorspace= gst_element_factory_make ("ffmpegcolorspace", "ffmpegcolorspace");
    jpegenc = gst_element_factory_make ("jpegenc", "jpegenc");
    filesink = gst_element_factory_make ("filesink","filesink");

    gst_bin_add (GST_BIN (imagecapturepipeline), ximagesrc);
    gst_bin_add (GST_BIN (imagecapturepipeline), ffmpegcolorspace);
    gst_bin_add (GST_BIN (imagecapturepipeline), jpegenc);
    gst_bin_add (GST_BIN (imagecapturepipeline), filesink);

    gst_element_link(ximagesrc, ffmpegcolorspace);
    gst_element_link(ffmpegcolorspace, jpegenc);
    gst_element_link(jpegenc, filesink);
}


void
createplaybackpipeline(void)
{

    playbackpipeline = gst_pipeline_new ("playbackpipeline");
    multifilesrc = gst_element_factory_make ("multifilesrc", "multifilesrc");
    mpegtsdemux = gst_element_factory_make ("ffdemux_mpegts", "mpegtsdemux");
    ffdech264 = gst_element_factory_make ("ffdec_h264", "ffdec_h264");
    xvimagesinkplayback = gst_element_factory_make ("xvimagesink","xvimagesinkplayback");

    gst_bin_add (GST_BIN (playbackpipeline), multifilesrc);
    gst_bin_add (GST_BIN (playbackpipeline), mpegtsdemux);
    gst_bin_add (GST_BIN (playbackpipeline), ffdech264);
    gst_bin_add (GST_BIN (playbackpipeline), xvimagesinkplayback);

    gst_element_link(multifilesrc, mpegtsdemux);
    g_signal_connect(mpegtsdemux,"pad-added",G_CALLBACK(mpegtsdemux_pad_added_cb),NULL);
    gst_element_link(ffdech264, xvimagesinkplayback);
}

void
createalarmpipeline(void)
{
    char uri[PATH_MAX];

    alarmpipeline = gst_pipeline_new ("alarmpipeline");
    playbin = gst_element_factory_make ("playbin", "playbin");
    sprintf(uri,"file://%s",conf.alarmplaybackfile);
    g_object_set(G_OBJECT(filesink),"uri",uri,NULL);
    gst_bin_add (GST_BIN (alarmpipeline), playbin);
}

gboolean
updateoverlay(gpointer data)
{
    time_t t;
    struct tm *tm;
    char buf[32];
    gchar *curloc;
    static int j=1;
    int i;

    t=time(NULL);
    tm=localtime(&t);
    for(i=0;i<conf.num;i++) {
	sprintf(buf,"%s %02d/%02d/%02d %02d:%02d:%02d",
		conf.names[i], tm->tm_mday, tm->tm_mon+1, tm->tm_year-100,
		tm->tm_hour, tm->tm_min, tm->tm_sec);
	g_object_set(G_OBJECT(cairotextoverlay[i]),"text", buf, NULL);
    }

    if(j)
	j++;

    if(j==2)
	startplayback(0,0);

    if(j==3) {
	stopplayback();
	j=0;
    }
    return TRUE;
}


void
startrecordpipeline(int i)
{
    gst_element_set_state (recordpipeline[i], GST_STATE_PLAYING);
}



void
startalarmpipeline(void)
{
    gst_element_set_state (alarmpipeline, GST_STATE_PLAYING);
}

void
destroypipelines(void)
{
    int i;

    for(i=0;i<conf.num;i++) {
	gst_element_set_state (recordpipeline[i], GST_STATE_NULL);
	gst_object_unref (recordpipeline[i]);
    }

    gst_element_set_state (playbackpipeline, GST_STATE_NULL);
    gst_object_unref (playbackpipeline);
    gst_element_set_state (alarmpipeline, GST_STATE_NULL);
    gst_object_unref (alarmpipeline);
    gst_element_set_state (alarmpipeline, GST_STATE_NULL);
    gst_object_unref (alarmpipeline);
}

void
initcaps(void)
{
    //viewing flash recording caps
    caps1 = gst_caps_new_simple ("video/x-raw-yuv",
	    "format", G_TYPE_STRING, "I420",
	    "width", G_TYPE_INT, conf.capwidth,
	    "height", G_TYPE_INT, conf.capheight,
	    "framerate", GST_TYPE_FRACTION, conf.framerate, 1,
	    NULL);
    //mobile caps
    caps2 = gst_caps_new_simple ("video/x-raw-yuv",
	    "format", G_TYPE_STRING, "I420",
	    "width", G_TYPE_INT, conf.mobilewidth,
	    "height", G_TYPE_INT, conf.mobileheight,
	    "framerate", GST_TYPE_FRACTION, conf.mobileframerate, 1,
	    NULL);
    // analysis caps
    caps3 = gst_caps_new_simple ("video/x-raw-yuv",
	    "format", G_TYPE_STRING, "I420",
	    "width", G_TYPE_INT, conf.capwidth,
	    "height", G_TYPE_INT, conf.capheight,
	    "framerate", GST_TYPE_FRACTION, conf.analysisframerate, 1,
	    NULL);
}

gint
main (gint argc, gchar *argv[])
{

    int i;

    gst_init (&argc, &argv);
    gtk_init (&argc, &argv);

    initconfig();
    initcaps();

    loop = g_main_loop_new (NULL, FALSE);

    for(i=0; i<conf.num; i++) {
	createrecordpipeline(i);
    }

    if(conf.enablealarmaudio)
	createalarmpipeline();

    //createplaybackpipeline();

    createui();
    createimagecapturepipeline();

    for(i=0;i<conf.num;i++) {
	startrecordpipeline(i);
    }

    g_timeout_add(500,updateoverlay,NULL);
    g_main_loop_run (loop);

    destroypipelines();
    return 0;
}





