#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <string.h>
#include "uvc-gadget.h"

#define UVC_DEBUG_ENABLE

// flags : 0 : init   ,1 : set attr
int uvc_input_StartCapture(void *uvc,int flags)
{
#ifdef UVC_DEBUG_ENABLE
    printf("[ %s start ]\n",__func__);
#endif

    if(!uvc || flags <0 || flags >1 )
    {
        printf(" %s Invalid Argument \n",__func__);
        return -EINVAL;
    }

    uvc_device *uvc_dev =(uvc_device*)uvc;
    uvc_set set = uvc_dev->set;

    // init input device
    switch(set.fcc)
    {   
        case V4L2_PIX_FMT_YUYV:
            printf("[ Video Format is V4L2_PIX_FMT_YUYV ]\n");
            break;
        case V4L2_PIX_FMT_MJPEG:
            printf("[ Video Format is VIDEO_PLAYLOAD_MJPEG ]\n");
            break;
        case V4L2_PIX_FMT_H264:
            printf("[ Video Format is VIDEO_PLAYLOAD_H264 ]\n");
            break;
        default:
            printf("[ Video Format is VIDEO_PLAYLOAD_MJPEG ]\n");
            break;
    }  

    /* Prepare Input Video Arguments */
    if(!flags)
    {

    }

    /* The first time to init input device,Input system Init*/
    if(!flags)
    {
        printf("[ First video_device_init ]\n");
    }

    /* Set the Video Attr */

#ifdef UVC_DEBUG_ENABLE
    printf("[ set->name %s ]\n",uvc_dev->set.name);
    printf("[ %s end ]\n",__func__);
#endif
    return 0;
}

int uvc_fillbuf(void *uvc,U32 *length,void *buf)
{ 

    if( !uvc || !length || !buf)
    {
        printf(" %s Invalid Argument exit ? uvc: %d length %d buf:%d \n",__func__,
        uvc!=NULL,length!=NULL,buf!=NULL);
        return -EINVAL;
    }

    uvc_device *uvc_dev =(uvc_device*)uvc;
    *length = 120;
 //   video_get_frame(Input_video,length,buf);

    return 0;
}

// flags : 0 : exit   ,1 : destroy attr
void uvc_input_StopCapture(void *uvc,int flags)
{
#ifdef UVC_DEBUG_ENABLE
    printf("[ %s start ]\n",__func__);
#endif

    if(!uvc || flags >1 || flags < 0)
    {
        printf(" %s Invalid Argument \n",__func__);
        return;
    }

    uvc_device *uvc_dev =(uvc_device*)uvc;
    
    /* Destroy the video attr */

    /* The last time and  Destroy video Sys Init */
    if(!flags)
    {
        printf("[ Last video_device_exit ]\n");
    }

#ifdef UVC_DEBUG_ENABLE
    printf("[ %s ]\n",__func__);
#endif
}

int main()
{
    printf("[ Enter Debug Mode ]\n");
    uvc_set set ={"/dev/video0",V4L2_PIX_FMT_MJPEG,1,1,640,480,25,31,
                    uvc_input_StartCapture,
                    uvc_fillbuf,
                    uvc_input_StopCapture };
    uvc_device uvc;
    memset(&uvc,0x00,sizeof(uvc_device));
    uvc_device_init(&uvc,set);
    uvc_process(&uvc);
    uvc_device_exit(&uvc);
    return 0;
}
