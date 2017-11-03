#include <linux/usb/ch9.h>
#include <linux/videodev2.h>

#include "video.h"
#include "uvc.h"

typedef char S8;
typedef unsigned char U8;
typedef unsigned int U32;
typedef unsigned int size_t;

#define MaxFrameSize    640*480*2   //define in kernel
#define MaxPayloadSize  640*480*2

#define ARRAY_SIZE(a)                  ((sizeof(a) / sizeof(a[0])))
#define CLEAR(x)                       memset(&(x), 0, sizeof (x))
#define FrameInterval2FrameRate(val)   ((int)(1.0/val*10000000))
#define FrameRate2FrameInterval(val)   ((int)(1.0/val*10000000))

#define clamp(val, min, max) ({                 \
        typeof(val) __val = (val);              \
        typeof(min) __min = (min);              \
        typeof(max) __max = (max);              \
        (void) (&__val == &__min);              \
        (void) (&__val == &__max);              \
        __val = __val < __min ? __min: __val;   \
        __val > __max ? __max: __val; })



typedef enum {
    UVC_PROBE_CONTROL = 1 ,
    UVC_COMMIT_CONTROL,
}control_type;

struct buffer {
    struct v4l2_buffer buf; 
    void *start;
    size_t length;
};

struct uvc_frame_info {
    unsigned int width;
    unsigned int height;
    unsigned int intervals[8];
};

struct uvc_format_info {
	unsigned int fcc;
	const struct uvc_frame_info *frames;
};

static const struct uvc_frame_info uvc_frames_yuyv[] = {
	{  640, 360, { 666666, 1000000, 5000000, 0 }, },
	{ 1280, 720, { 5000000, 0 }, },
	{ 0, 0, { 0, }, },
};

static const struct uvc_frame_info uvc_frames_mjpeg[] = {
	{  640,  360, { 333333, 666666, 1000000, 0 }, },
	{  640,  480, { 333333, 666666, 1000000, 0 }, },
    { 1280,  720, { 333333, 666666, 1000000, 0 }, },
    { 1920, 1080, { 400000, 666666, 1000000, 0 }, },
	{ 0, 0, { 0, }, },
};

static const struct uvc_frame_info uvc_frames_h264[] = {
	{  640, 360, { 666666, 1000000, 5000000, 0 }, },
	{ 1280, 720, { 5000000, 0 }, },
	{ 0, 0, { 0, }, },
};

static const struct uvc_format_info uvc_formats[] = {
	{ V4L2_PIX_FMT_YUYV, uvc_frames_yuyv },
	{ V4L2_PIX_FMT_MJPEG, uvc_frames_mjpeg },
//	{ V4L2_PIX_FMT_H264, uvc_frames_h264 },
};

typedef struct {
    /* uvc device */
    char *name;
   /* video format framerate */
    U32 fcc;      
    U32 iformat;   //format index
    U32 iframe;    //frame index
    U32 width;
    U32 height;
    double frameRate;

   /* buffer related*/
    U8 nbuf;

    /*Function to Fill a buff
     *   flags: 
     *          1: only for set attr or destroy attr
     *          0: Init the video or destroy 
     */
    int (* uvc_input_StartCapture)(void *uvc,int flags);
    int (* uvc_fillbuf)(void *uvc,U32 *length,void *buf);
    void (* uvc_input_StopCaputre)(void *uvc,int flags);
} uvc_set;

typedef struct {  //only for bulk mode
    int uvc_fd;
     
    // for video Setting
    uvc_set set;
    
    // for Input Video Param
    void *Input_device;

	/* uvc control request specific */
    control_type control;
    struct uvc_request_data request_error_code;
	struct uvc_streaming_control probe;
	struct uvc_streaming_control commit;

    // for buffer
    struct buffer *mem;

    //uvc specific flags
    int has_reqbufs;
    int uvc_device_inited;
    int input_video_straming;     // for Input video 
    int output_video_straming;    // for output video 
    int uvc_shutdown_requested;   // UVC Shutdown ?

} uvc_device;
 
int uvc_device_init(uvc_device *uvc,uvc_set param);
void uvc_device_exit(uvc_device *uvc);
void uvc_process(uvc_device *uvc);
