#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <signal.h>

#include "uvc-gadget.h"

//#define DBUG_UVC_DEVICE 

static int uvc_crash;
static void uvc_device_int();

static void uvc_fill_streaming_control(struct uvc_streaming_control *ctrl,
                                       int iframe, int iformat)
{
	const struct uvc_format_info *format;
	const struct uvc_frame_info *frame;
	unsigned int nframes;

	if (iformat < 0)
		iformat = ARRAY_SIZE(uvc_formats) + iformat;
	if (iformat < 0 || iformat >= (int)ARRAY_SIZE(uvc_formats))
		return;
	format = &uvc_formats[iformat];

	nframes = 0;
	while (format->frames[nframes].width != 0)
		++nframes;

	if (iframe < 0)
		iframe = nframes + iframe;
	if (iframe < 0 || iframe >= (int)nframes)
		return;
	frame = &format->frames[iframe];

	memset(ctrl, 0, sizeof *ctrl);

	ctrl->bmHint = 1;
	ctrl->bFormatIndex = iformat + 1;
	ctrl->bFrameIndex = iframe + 1;
	ctrl->dwFrameInterval = frame->intervals[0];
	switch (format->fcc) {
	case V4L2_PIX_FMT_YUYV:
		ctrl->dwMaxVideoFrameSize = frame->width * frame->height * 1.5;
		break;
	case V4L2_PIX_FMT_MJPEG:
	case V4L2_PIX_FMT_H264:
		ctrl->dwMaxVideoFrameSize = MaxFrameSize;
		break;
	}

	/* TODO: the UVC maxpayload transfer size should be filled
	 * by the driver.
	 */
	ctrl->dwMaxPayloadTransferSize = MaxFrameSize;

	ctrl->bmFramingInfo = 3;
	ctrl->bPreferedVersion = 1;
	ctrl->bMaxVersion = 1;
}
 
void uvc_device_init(uvc_device *uvc,uvc_set param)
{
    int ret = -1;
    struct v4l2_event_subscription sub;
    struct v4l2_capability cap;

/* Check the UVC DEVICE */
    if(!uvc)
    {
        printf("Error: Please Input a uvc device struct\n");
        return;
    }

/* Input Device Func & Video Format & Request Probe and Commit Init */
    if(    param.uvc_input_StartCapture 
        && param.uvc_fillbuf 
        && param.uvc_input_StopCaputre )
    {
        uvc->set = param;
    } else{
        printf("Init UVC Device fail ,lack of Input device Func \n");
        return;
    }
#ifdef DBUG_UVC_DEVICE
    printf("[ %s iformat %d iframe %d width %d height %d ]\n",__func__,
            uvc->set.iformat,uvc->set.iframe,uvc->set.width,uvc->set.height);
#endif

/* Streaming control param */
    uvc->control   = UVC_PROBE_CONTROL;
    uvc_fill_streaming_control(&uvc->probe,uvc->set.iformat,uvc->set.iframe);
    uvc_fill_streaming_control(&uvc->commit,uvc->set.iformat,uvc->set.iframe);
    uvc->probe.dwMaxPayloadTransferSize = MaxFrameSize;
    uvc->commit.dwMaxPayloadTransferSize = MaxFrameSize;
    uvc->set.frameRate = FrameInterval2FrameRate(uvc->commit.dwFrameInterval);
#ifdef DBUG_UVC_DEVICE
    printf("[ %s probe iformat %d iframe %d dwFrameInterval %d frameRate %d ]\n",__func__,
           uvc->probe.bFormatIndex,uvc->probe.bFrameIndex,uvc->probe.dwFrameInterval,uvc->set.frameRate);
#endif

/* UVC Specific flags Init */
    uvc->has_reqbufs            = 0;
    uvc->input_video_straming   = 0;
    uvc->output_video_straming  = 0;
    uvc->uvc_shutdown_requested = 0;

/* Start Init the UVC DEVICE */
   /* open the uvc device */
    if(param.name == NULL)
    {
        printf("[ %s the uvc can't be opened ]\n",__func__);
        return;
    }
    uvc->uvc_fd = open(param.name, O_RDWR | O_NONBLOCK);
    if (uvc->uvc_fd == -1) {
        printf("UVC: device open failed: %s (%d).\n",
               strerror(errno), errno);
        return; 
    }
   /* query uvc device */
    ret = ioctl(uvc->uvc_fd, VIDIOC_QUERYCAP, &cap);
    if (ret < 0) {
        printf("UVC: unable to query uvc device: %s (%d)\n",
                strerror(errno), errno);
        goto err;
    }
   /* check the device type */
    if (!(cap.capabilities & V4L2_CAP_VIDEO_OUTPUT)) {
        printf("UVC: %s is no video output device\n", param.name);
        goto err;
    }
#ifdef DBUG_UVC_DEVICE
    printf("uvc device is %s on bus %s\n", cap.card, cap.bus_info);
    printf("uvc open succeeded, file descriptor = %d\n", uvc->uvc_fd);
#endif
    
   /* add the subscribe event to the uvc */  
    memset(&sub, 0, sizeof sub);
    sub.type = UVC_EVENT_SETUP;
    ioctl(uvc->uvc_fd, VIDIOC_SUBSCRIBE_EVENT, &sub);
    sub.type = UVC_EVENT_DATA;
    ioctl(uvc->uvc_fd, VIDIOC_SUBSCRIBE_EVENT, &sub);
    sub.type = UVC_EVENT_STREAMON;
    ioctl(uvc->uvc_fd, VIDIOC_SUBSCRIBE_EVENT, &sub);
    sub.type = UVC_EVENT_STREAMOFF;
    ioctl(uvc->uvc_fd, VIDIOC_SUBSCRIBE_EVENT, &sub);
   
    /* Exit the function with Ctrl-C */
    signal(SIGINT,uvc_device_int);

    return;

err:
   close(uvc->uvc_fd);
   return;
}

static S8 uvc_uninit_device(uvc_device *uvc)
{
	unsigned int i;
	int ret;

	for (i = 0; i < uvc->set.nbuf; ++i) {
		ret = munmap(uvc->mem[i].start, uvc->mem[i].length);
		if (ret < 0) {
			printf("UVC: munmap failed\n");
			return ret;
		}
	}
	free(uvc->mem);

	return 0;
}

static S8 uvc_video_reqbufs(uvc_device *uvc,int flags)
{
	struct v4l2_requestbuffers rb;
	unsigned int i;
	int ret;

    struct buffer *mem = NULL;
    uvc_set set = uvc->set;
    int uvc_fd = uvc->uvc_fd;

	CLEAR(rb);

    if(1 == flags)
	    rb.count = set.nbuf;
    else if(0 == flags)
        rb.count = 0;
    else
        return -EINVAL;

	rb.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	rb.memory = V4L2_MEMORY_MMAP;

	ret = ioctl(uvc_fd, VIDIOC_REQBUFS, &rb);
	if (ret < 0) {
		if (ret == -EINVAL)
			printf("UVC: does not support memory mapping\n");
		else
			printf("UVC: Unable to allocate buffers: %s (%d).\n",
					strerror(errno), errno);
		goto err;
	}

	if (!rb.count)
		return 0;

	if (rb.count < 2) {
		printf("UVC: Insufficient buffer memory.\n");
		ret = -EINVAL;
		goto err;
	}

	/* Map the buffers. */
	mem = (struct buffer*)calloc(rb.count, sizeof(mem[0]));
	if (!mem) {
		printf("UVC: Out of memory\n");
		ret = -ENOMEM;
		goto err;
	}

	for (i = 0; i < rb.count; ++i) {
		memset(&mem[i].buf, 0, sizeof(mem[i].buf));

		mem[i].buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		mem[i].buf.memory = V4L2_MEMORY_MMAP;
		mem[i].buf.index = i;

		ret = ioctl(uvc_fd, VIDIOC_QUERYBUF, &(mem[i].buf));
		if (ret < 0) {
			printf("UVC: VIDIOC_QUERYBUF failed for buf %d: "
				"%s (%d).\n", i, strerror(errno), errno);
			ret = -EINVAL;
			goto err_free;
		}

		mem[i].start = mmap(NULL /* start anywhere */,
					mem[i].buf.length,
					PROT_READ | PROT_WRITE /* required */,
					MAP_SHARED /* recommended */,
					uvc_fd, mem[i].buf.m.offset);

		if (MAP_FAILED == mem[i].start) {
			printf("[ %s UVC: Unable to map buffer %u: %s (%d). ]\n", __func__,i,
				strerror(errno), errno);
			mem[i].length = 0;
			ret = -EINVAL;
			goto err_free;
		}

		mem[i].length = mem[i].buf.length;
#ifdef UVC_DEBUG_ENABLE
		printf("[ %s UVC: Buffer %u mapped at address %p mem length %d. ]\n",__func__, i,
				mem[i].start,mem[i].length);
#endif
	}

	uvc->set.nbuf = rb.count;
    uvc->mem = mem;
	printf("[ %s UVC: %u buffers allocated. ]\n",__func__, rb.count);
    
	return 0;

err_free:
	free(mem);
err:
	return ret;
}
    

static S8 uvc_video_stream(int fd,int enable)
{
	int type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	int ret;

	if (!enable) {
		ret = ioctl(fd, VIDIOC_STREAMOFF, &type);
		if (ret < 0) {
			printf("UVC: VIDIOC_STREAMOFF failed: %s (%d).\n",
					strerror(errno), errno);
			return ret;
		}

		printf("UVC: Stopping video stream.\n");

		return 0;
	} else {
      ret = ioctl(fd, VIDIOC_STREAMON, &type);
	  if (ret < 0) {
	  	  printf("UVC: Unable to start streaming %s (%d).\n",
			  strerror(errno), errno);
		  return ret;
	  } 
      printf("[ UVC: Starting video stream.]\n");
      return 0;
     }
      return 0;
}

static S8 uvc_video_qbuf(uvc_device *uvc)
{
	unsigned int i;
	int ret;
    
    if(!uvc){
         printf(" %s Invalid Arguments\n",__func__);
         return -EINVAL;
    }

    int uvc_fd = uvc->uvc_fd;
    uvc_set set = uvc->set;
    struct buffer *mem = uvc->mem;

	for (i = 0; i < set.nbuf; ++i) {
		memset(&mem[i].buf, 0, sizeof(mem[i].buf));

		mem[i].buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		mem[i].buf.memory = V4L2_MEMORY_MMAP;
		mem[i].buf.index = i;
       
        // fill uvc buffer  MaxBufSize 153600
        ret = set.uvc_fillbuf(uvc,&mem[i].buf.bytesused,mem[i].start);
        if( ret <0 )
        {
            printf(" %s some thing error in fill data\n",__func__);
            return ret;
        }
        
        // queue  the uvc buffer
		ret = ioctl(uvc_fd, VIDIOC_QBUF, &(mem[i].buf));
		if (ret < 0) {
			printf("UVC: VIDIOC_QBUF failed : %s (%d).\n",
					strerror(errno), errno);
			return ret;
		}
	}

	return 0;
}


static S8 uvc_handle_streamon_event(uvc_device *uvc)
{
	int ret;

    uvc_set set = uvc->set;
    int uvc_fd = uvc->uvc_fd;

    /* Request UVC buffers & mmap  */
    if(!uvc->has_reqbufs)
    {
  	    ret = uvc_video_reqbufs(uvc,1);
	    if (ret < 0)
		    goto err;
        uvc->has_reqbufs = 1;
    }

	/* Start Input Video capturing now. */
    if(!uvc->input_video_straming)
    {   /* the first to init the input device */
	    ret = set.uvc_input_StartCapture(uvc,0);
	    if (ret < 0)
		    goto err;
	    uvc->input_video_straming = 1;
    } else {
        /* only set the attr of input device */
        set.uvc_input_StopCaputre(uvc,1);
	    ret = set.uvc_input_StartCapture(uvc,1);
	    if (ret < 0)
		    goto err;
	    uvc->input_video_straming = 1;
    }

	/* Queue buffers to UVC domain and start streaming. */
    if(!uvc->output_video_straming)
    {
	    ret = uvc_video_qbuf(uvc);
	    if (ret < 0)
		    goto err;
        /* UVC Device Streamon */
        uvc_video_stream(uvc_fd,1);
    } 

    uvc->output_video_straming = 1;
    uvc->uvc_shutdown_requested = 0;
#ifdef DBUG_UVC_DEVICE
    printf("[ %s start streaming ]\n",__func__);
#endif
	return 0;

err:
	return ret;
}

static S8 uvc_events_process_control(uvc_device *uvc,U8 req,U8 cs,U8 entity_id,U8 len, 
                                          struct uvc_request_data *resp)
{
	switch (entity_id) {
	case 0:
		switch (cs) {
		case UVC_VC_REQUEST_ERROR_CODE_CONTROL:
			/* Send the request error code last prepared. */
			resp->data[0] = uvc->request_error_code.data[0];
			resp->length  = uvc->request_error_code.length;
			break;

		default:
			/*
			 * If we were not supposed to handle this
			 * 'cs', prepare an error code response.
			 */
			uvc->request_error_code.data[0] = 0x06;
			uvc->request_error_code.length = 1;
			break;
		}
		break;

	/* Camera terminal unit 'UVC_VC_INPUT_TERMINAL'. */
	case 1:
		switch (cs) {
		/*
		 * We support only 'UVC_CT_AE_MODE_CONTROL' for CAMERA
		 * terminal, as our bmControls[0] = 2 for CT. Also we
		 * support only auto exposure.
		 */
		case UVC_CT_AE_MODE_CONTROL:
			switch (req) {
			case UVC_SET_CUR:
				/* Incase of auto exposure, attempts to
				 * programmatically set the auto-adjusted
				 * controls are ignored.
				 */
				resp->data[0] = 0x01;
				resp->length = 1;
				/*
				 * For every successfully handled control
				 * request set the request error code to no
				 * error.
				 */
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
				break;

			case UVC_GET_INFO:
				/*
				 * TODO: We support Set and Get requests, but
				 * don't support async updates on an video
				 * status (interrupt) endpoint as of
				 * now.
				 */
				resp->data[0] = 0x03;
				resp->length = 1;
				/*
				 * For every successfully handled control
				 * request set the request error code to no
				 * error.
				 */
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
				break;

			case UVC_GET_CUR:
			case UVC_GET_DEF:
			case UVC_GET_RES:
				/* Auto Mode â?? auto Exposure Time, auto Iris. */
				resp->data[0] = 0x02;
				resp->length = 1;
				/*
				 * For every successfully handled control
				 * request set the request error code to no
				 * error.
				 */
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
				break;
			default:
				/*
				 * We don't support this control, so STALL the
				 * control ep.
				 */
				resp->length = -EL2HLT;
				/*
				 * For every unsupported control request
				 * set the request error code to appropriate
				 * value.
				 */
				uvc->request_error_code.data[0] = 0x07;
				uvc->request_error_code.length = 1;
				break;
			}
			break;
#if 1
        case UVC_CT_EXPOSURE_TIME_ABSOLUTE_CONTROL:
			switch (req) {
			case UVC_SET_CUR:
				resp->data[0] = 0x64;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
            case UVC_GET_CUR:	
				resp->data[0] = 0x64;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
            case UVC_GET_INFO:
				resp->data[0] = 0x0f;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
            case UVC_GET_MIN:
				resp->data[0] = 0x64;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
            case UVC_GET_MAX:
				resp->data[0] = 0x60;
                resp->data[1] = 0x09;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
            case UVC_GET_RES:
				resp->data[0] = 0x64;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
            case UVC_GET_DEF:
				resp->data[0] = 0x2c;
                resp->data[1] = 0x01;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
            default:
				resp->length = -EL2HLT;
				uvc->request_error_code.data[0] = 0x07;
				uvc->request_error_code.length = 1;
				break;
			}
            break;
#endif
#if 1
        case UVC_CT_IRIS_ABSOLUTE_CONTROL:
				resp->length = -EL2HLT;
				uvc->request_error_code.data[0] = 0x07;
				uvc->request_error_code.length = 1;
            break;
#endif
#if 1
        case UVC_CT_ZOOM_ABSOLUTE_CONTROL:
			switch (req) {
            case UVC_GET_INFO:
				resp->data[0] = 0x0b;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
            case UVC_GET_MIN:
				resp->data[0] = 0x00;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
            case UVC_GET_MAX:
				resp->data[0] = 0x1;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
            case UVC_GET_RES:
				resp->data[0] = 0x1;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
			case UVC_SET_CUR:
            case UVC_GET_CUR:	
            case UVC_GET_DEF:
				resp->data[0] = 0x00;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
            default:
				resp->length = -EL2HLT;
				uvc->request_error_code.data[0] = 0x07;
				uvc->request_error_code.length = 1;
				break;
			}
            break;
#endif
		default:
			/*
			 * We don't support this control, so STALL the control
			 * ep.
			 */
			resp->length = -EL2HLT;
			/*
			 * If we were not supposed to handle this
			 * 'cs', prepare a Request Error Code response.
			 */
			uvc->request_error_code.data[0] = 0x06;
			uvc->request_error_code.length = 1;
			break;
		}
		break;

	/* processing unit 'UVC_VC_PROCESSING_UNIT' */
	case 2:
		switch (cs) {
		/*
		 * We support only 'UVC_PU_BRIGHTNESS_CONTROL' for Processing
		 * Unit, as our bmControls[0] = 1 for PU.
		 */
        case UVC_PU_BACKLIGHT_COMPENSATION_CONTROL:
            switch (req) {
			case UVC_SET_CUR:
				resp->data[0] = 0x0;
				resp->length = len;
				/*
				 * For every successfully handled control
				 * request set the request error code to no
				 * error
				 */
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
				break;
			case UVC_GET_MIN:
				resp->data[0] = 0x0;
				resp->length = 2;
				/*
				 * For every successfully handled control
				 * request set the request error code to no
				 * error
				 */
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
				break;
			case UVC_GET_MAX:
				resp->data[0] = 0x1;
				resp->data[1] = 0x0;
				resp->length = 2;
				/*
				 * For every successfully handled control
				 * request set the request error code to no
				 * error
				 */
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
				break;
			case UVC_GET_CUR:
				resp->length = 2;
	//			memcpy(&resp->data[0], &brightness_val,
	//					resp->length);
				/*
				 * For every successfully handled control
				 * request set the request error code to no
				 * error
				 */
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
				break;
			case UVC_GET_INFO:
				/*
				 * We support Set and Get requests and don't
				 * support async updates on an interrupt endpt
				 */
				resp->data[0] = 0x03;
				resp->length = 1;
				/*
				 * For every successfully handled control
				 * request, set the request error code to no
				 * error.
				 */
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
				break;
			case UVC_GET_DEF:
				resp->data[0] = 0x2;
				resp->data[1] = 0x0;
				resp->length = 2;
				/*
				 * For every successfully handled control
				 * request, set the request error code to no
				 * error.
				 */
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
				break;
			case UVC_GET_RES:
				resp->data[0] = 0x1;
				resp->data[1] = 0x0;
				resp->length = 2;
				/*
				 * For every successfully handled control
				 * request, set the request error code to no
				 * error.
				 */
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
				break;
			default:
				/*
				 * We don't support this control, so STALL the
				 * default control ep.
				 */
				resp->length = -EL2HLT;
				/*
				 * For every unsupported control request
				 * set the request error code to appropriate
				 * code.
				 */
				uvc->request_error_code.data[0] = 0x07;
				uvc->request_error_code.length = 1;
				break;
            }
		case UVC_PU_BRIGHTNESS_CONTROL:
			switch (req) {
			case UVC_SET_CUR:
				resp->data[0] = 0x0;
				resp->length = len;
				/*
				 * For every successfully handled control
				 * request set the request error code to no
				 * error
				 */
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
				break;
			case UVC_GET_MIN:
				//resp->data[0] = PU_BRIGHTNESS_MIN_VAL;
				resp->data[0] = 0;
				resp->length = 2;
				/*
				 * For every successfully handled control
				 * request set the request error code to no
				 * error
				 */
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
				break;
			case UVC_GET_MAX:
			//	resp->data[0] = PU_BRIGHTNESS_MAX_VAL;
				resp->data[0] = 255;
				resp->length = 2;
				/*
				 * For every successfully handled control
				 * request set the request error code to no
				 * error
				 */
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
				break;
			case UVC_GET_CUR:
				resp->length = 2;
			//	memcpy(&resp->data[0], &brightness_val,
			//			resp->length);
				/*
				 * For every successfully handled control
				 * request set the request error code to no
				 * error
				 */
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
				break;
			case UVC_GET_INFO:
				/*
				 * We support Set and Get requests and don't
				 * support async updates on an interrupt endpt
				 */
				resp->data[0] = 0x03;
				resp->length = 1;
				/*
				 * For every successfully handled control
				 * request, set the request error code to no
				 * error.
				 */
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
				break;
			case UVC_GET_DEF:
			//	resp->data[0] = PU_BRIGHTNESS_DEFAULT_VAL;
				resp->data[0] = 127;
				resp->length = 2;
				/*
				 * For every successfully handled control
				 * request, set the request error code to no
				 * error.
				 */
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
				break;
			case UVC_GET_RES:
			//	resp->data[0] = PU_BRIGHTNESS_STEP_SIZE;
				resp->data[0] = 1;
				resp->length = 2;
				/*
				 * For every successfully handled control
				 * request, set the request error code to no
				 * error.
				 */
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
				break;
			default:
				/*
				 * We don't support this control, so STALL the
				 * default control ep.
				 */
				resp->length = -EL2HLT;
				/*
				 * For every unsupported control request
				 * set the request error code to appropriate
				 * code.
				 */
				uvc->request_error_code.data[0] = 0x07;
				uvc->request_error_code.length = 1;
				break;
			}
			break;

#if 1
        case UVC_PU_CONTRAST_CONTROL:
			switch (req) {
            case UVC_GET_INFO:
				resp->data[0] = 0x03;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
            case UVC_GET_MIN:
				resp->data[0] = 0x01;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
            case UVC_GET_MAX:
				resp->data[0] = 0x20;
				resp->data[1] = 0x00;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
            case UVC_GET_RES:
				resp->data[0] = 0x1;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
			case UVC_SET_CUR:
            case UVC_GET_CUR:	
            case UVC_GET_DEF:
				resp->data[0] = 0x01;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
            default:
				resp->length = -EL2HLT;
				uvc->request_error_code.data[0] = 0x07;
				uvc->request_error_code.length = 1;
				break;
			}
            break;
#endif
#if 1
        case UVC_PU_GAIN_CONTROL:
			switch (req) {
            case UVC_GET_INFO:
				resp->data[0] = 0x03;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
            case UVC_GET_MIN:
				resp->data[0] = 0x30;
				resp->data[1] = 0x00;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
            case UVC_GET_MAX:
				resp->data[0] = 0x60;
				resp->data[1] = 0x00;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
            case UVC_GET_RES:
				resp->data[0] = 0x01;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
			case UVC_SET_CUR:
            case UVC_GET_CUR:	
            case UVC_GET_DEF:
				resp->data[0] = 0x40;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
            default:
				resp->length = -EL2HLT;
				uvc->request_error_code.data[0] = 0x07;
				uvc->request_error_code.length = 1;
				break;
			}
            break;
#endif
#if 1
        case UVC_PU_POWER_LINE_FREQUENCY_CONTROL:
			switch (req) {
            case UVC_GET_INFO:
				resp->data[0] = 0x03;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
            case UVC_GET_MIN:
				resp->data[0] = 0x00;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
            case UVC_GET_MAX:
				resp->data[0] = 0x02;
				resp->data[1] = 0x00;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
            case UVC_GET_RES:
				resp->data[0] = 0x01;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
			case UVC_SET_CUR:
            case UVC_GET_CUR:	
            case UVC_GET_DEF:
				resp->data[0] = 0x01;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
            default:
				resp->length = -EL2HLT;
				uvc->request_error_code.data[0] = 0x07;
				uvc->request_error_code.length = 1;
				break;
			}
            break;
#endif
#if 1
        case UVC_PU_HUE_CONTROL:
			switch (req) {
            case UVC_GET_INFO:
				resp->data[0] = 0x03;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
            case UVC_GET_MIN:
				resp->data[0] = 0x00;
				resp->data[1] = 0x2d;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
            case UVC_GET_MAX:
				resp->data[0] = 0xd3;
				resp->data[1] = 0xff;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
            case UVC_GET_RES:
			case UVC_SET_CUR:
            case UVC_GET_CUR:	
            case UVC_GET_DEF:
				resp->data[0] = 0x01;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
            default:
				resp->length = -EL2HLT;
				uvc->request_error_code.data[0] = 0x07;
				uvc->request_error_code.length = 1;
				break;
			}
            break;
#endif
#if 1
        case UVC_PU_SATURATION_CONTROL:
			switch (req) {
            case UVC_GET_INFO:
				resp->data[0] = 0x03;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
            case UVC_GET_MIN:
				resp->data[0] = 0x00;
				resp->data[0] = 0x00;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
            case UVC_GET_MAX:
				resp->data[0] = 0x13;
				resp->data[1] = 0;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
            case UVC_GET_RES:
			case UVC_SET_CUR:
            case UVC_GET_CUR:	
            case UVC_GET_DEF:
				resp->data[0] = 0x01;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
            default:
				resp->length = -EL2HLT;
				uvc->request_error_code.data[0] = 0x07;
				uvc->request_error_code.length = 1;
				break;
			}
            break;
#endif
#if 1
        case UVC_PU_SHARPNESS_CONTROL:
			switch (req) {
            case UVC_GET_INFO:
				resp->data[0] = 0x03;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
            case UVC_GET_MIN:
				resp->data[0] = 0x00;
				resp->data[0] = 0x00;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
            case UVC_GET_MAX:
				resp->data[0] = 0x0f;
				resp->data[1] = 0;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
            case UVC_GET_RES:
			case UVC_SET_CUR:
            case UVC_GET_CUR:	
            case UVC_GET_DEF:
				resp->data[0] = 0x02;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
            default:
				resp->length = -EL2HLT;
				uvc->request_error_code.data[0] = 0x07;
				uvc->request_error_code.length = 1;
				break;
			}
            break;
#endif
#if 1
        case UVC_PU_GAMMA_CONTROL:
			switch (req) {
            case UVC_GET_INFO:
				resp->data[0] = 0x03;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
            case UVC_GET_MIN:
				resp->data[0] = 0x64;
				resp->data[0] = 0x00;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
            case UVC_GET_MAX:
				resp->data[0] = 0xbe;
				resp->data[1] = 0;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
            case UVC_GET_RES:
				resp->data[0] = 0x0a;
				resp->data[1] = 0;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
			case UVC_SET_CUR:
            case UVC_GET_CUR:	
            case UVC_GET_DEF:
				resp->data[0] = 0x96;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
            default:
				resp->length = -EL2HLT;
				uvc->request_error_code.data[0] = 0x07;
				uvc->request_error_code.length = 1;
				break;
			}
            break;
#endif
#if 1
        case UVC_PU_WHITE_BALANCE_TEMPERATURE_CONTROL:
			switch (req) {
            case UVC_GET_INFO:
				resp->data[0] = 0x0f;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
            case UVC_GET_MIN:
				resp->data[0] = 0xf0;
				resp->data[0] = 0x0a;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
            case UVC_GET_MAX:
				resp->data[0] = 0x64;
				resp->data[1] = 0x19;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
            case UVC_GET_RES:
				resp->data[0] = 0x3a;
				resp->data[1] = 0x07;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
			case UVC_SET_CUR:
            case UVC_GET_CUR:	
            case UVC_GET_DEF:
				resp->data[0] = 0x19;
				resp->data[0] = 0x64;
				resp->length = len;
				uvc->request_error_code.data[0] = 0x00;
				uvc->request_error_code.length = 1;
                break;
            default:
				resp->length = -EL2HLT;
				uvc->request_error_code.data[0] = 0x07;
				uvc->request_error_code.length = 1;
				break;
			}
            break;
#endif
		default:
			/*
			 * We don't support this control, so STALL the control
			 * ep.
			 */
			resp->length = -EL2HLT;
			/*
			 * If we were not supposed to handle this
			 * 'cs', prepare a Request Error Code response.
			 */
			uvc->request_error_code.data[0] = 0x06;
			uvc->request_error_code.length = 1;
			break;
		}

		break;

	default:
		/*
		 * If we were not supposed to handle this
		 * 'cs', prepare a Request Error Code response.
		 */
		uvc->request_error_code.data[0] = 0x06;
		uvc->request_error_code.length = 1;
		break;

	}
    if(resp->length < 0)
    {
		resp->data[0] = 0x5;
		resp->length = len;
    }

#ifdef UVC_DEBUG_ENABLE
	printf("[ %s control request (req %02x cs %02x) ]\n", __func__,req, cs);
#endif
    return 0;
}

static S8 uvc_events_process_streaming(uvc_device *uvc,U8 req, U8 cs,
                                            struct uvc_request_data *resp)
{
	struct uvc_streaming_control *ctrl;

	if (cs != UVC_VS_PROBE_CONTROL && cs != UVC_VS_COMMIT_CONTROL)
		return -EINVAL;

	ctrl = (struct uvc_streaming_control *)&resp->data;
	resp->length = sizeof *ctrl;

	switch (req) {
	case UVC_SET_CUR:
		uvc->control =(control_type)cs;
		resp->length = 34;
		break;

	case UVC_GET_CUR:
		if (cs == UVC_VS_PROBE_CONTROL)
			memcpy(ctrl, &uvc->probe, sizeof *ctrl);
		else
			memcpy(ctrl, &uvc->commit, sizeof *ctrl);
#ifdef UVC_DEBUG_ENABLE
        printf("Format Index  : %d\n",ctrl->bFormatIndex);  
        printf("Frame  Index  : %d\n",ctrl->bFrameIndex);
        printf("Frame  Interval   : %d\n",ctrl->dwFrameInterval);
        printf("Frame  Rate   : %d\n",ctrl->wPFrameRate);
#endif
		break;

	case UVC_GET_MIN:
	case UVC_GET_MAX:
	case UVC_GET_DEF:
		uvc_fill_streaming_control(ctrl, req == UVC_GET_MAX ? -1 : 0,
					   req == UVC_GET_MAX ? -1 : 0);
		break;

	case UVC_GET_RES:
		CLEAR(ctrl);
		break;

	case UVC_GET_LEN:
		resp->data[0] = 0x00;
		resp->data[1] = 0x22;
		resp->length = 2;
		break;

	case UVC_GET_INFO:
		resp->data[0] = 0x03;
		resp->length = 1;
		break;
	}
    return 0;
}

static S8 uvc_events_process_class(uvc_device *uvc,struct usb_ctrlrequest *ctrl,
                                        struct uvc_request_data *resp)
{
	if ((ctrl->bRequestType & USB_RECIP_MASK) != USB_RECIP_INTERFACE)
		return -EINVAL;

	switch (ctrl->wIndex & 0xff) {
	case UVC_INTF_CONTROL:
		uvc_events_process_control(uvc,
                                   ctrl->bRequest,
			    	     		   ctrl->wValue >> 8,
					        	   ctrl->wIndex >> 8,
						           ctrl->wLength, resp);
		break;

	case UVC_INTF_STREAMING:
		uvc_events_process_streaming(uvc,
                                     ctrl->bRequest,
						             ctrl->wValue >> 8, resp);
		break;

	default:
		break;
	}
    return 0;
}

static S8 uvc_events_process_standard(uvc_device *uvc,struct usb_ctrlrequest *ctrl,
                                           struct uvc_request_data *resp)
{
	(void)ctrl;
	(void)resp;
    return 0;
}

static S8 uvc_events_process_setup(uvc_device *uvc,struct usb_ctrlrequest *ctrl,
                                        struct uvc_request_data *resp)
{
#ifdef ENABLE_USB_REQUEST_DEBUG
	printf("\n[ %s bRequestType %02x bRequest %02x wValue %04x wIndex %04x "
		"wLength %04x ]\n",__func__, ctrl->bRequestType, ctrl->bRequest,
		ctrl->wValue, ctrl->wIndex, ctrl->wLength);
#endif

	switch (ctrl->bRequestType & USB_TYPE_MASK) {
	case USB_TYPE_STANDARD:
		uvc_events_process_standard(uvc,ctrl, resp);
		break;

	case USB_TYPE_CLASS:
		uvc_events_process_class(uvc,ctrl, resp);
		break;

	default:
		break;
	}
    return 0;
}


static S8 uvc_events_process_data(uvc_device *uvc,struct uvc_request_data *data)
{
	struct uvc_streaming_control *target = NULL ;
	struct uvc_streaming_control *ctrl = NULL;
	const struct uvc_format_info *format = NULL ;
	const struct uvc_frame_info *frame = NULL ;
	const unsigned int *interval = NULL;
	unsigned int iformat_tmp, iframe_tmp;
	unsigned int nframes;
	int ret;

	switch (uvc->control) {
	case UVC_VS_PROBE_CONTROL:
		printf("[ %s setting probe control, length = %d ]\n", __func__,data->length);
		target = &uvc->probe;
		break;

	case UVC_VS_COMMIT_CONTROL:
		printf("[ %s setting commit control, length = %d ]\n", __func__,data->length);
		target = &uvc->commit;
		break;

	default:
		printf("[ %s setting unknown control, length = %d ]\n", __func__,data->length);
    }

	ctrl = (struct uvc_streaming_control *)&data->data;
	iformat_tmp = clamp((unsigned int)ctrl->bFormatIndex, 1U,
			(unsigned int)ARRAY_SIZE(uvc_formats));

	format = &uvc_formats[iformat_tmp-1];

	nframes = 0;
	while (format->frames[nframes].width != 0)
		++nframes;

	iframe_tmp = clamp((unsigned int)ctrl->bFrameIndex, 1U, nframes);
	frame = &format->frames[iframe_tmp-1];
	interval = frame->intervals;

	while (interval[0] < ctrl->dwFrameInterval && interval[1])
		++interval;
	target->bFormatIndex = iformat_tmp;
	target->bFrameIndex = iframe_tmp;
	switch (format->fcc) {
	case V4L2_PIX_FMT_YUYV:
		target->dwMaxVideoFrameSize = frame->width * frame->height * 1.5;
		break;
	case V4L2_PIX_FMT_MJPEG:
	case V4L2_PIX_FMT_H264:
		target->dwMaxVideoFrameSize = MaxFrameSize;
		break;
	}
	target->dwFrameInterval = *interval;
	
	if (uvc->control == UVC_VS_COMMIT_CONTROL) {
        uvc->set.fcc       = format->fcc ;
        uvc->set.iformat   = iformat_tmp;
        uvc->set.iframe    = iframe_tmp;
		uvc->set.width     = frame->width;
		uvc->set.height    = frame->height;
        uvc->set.frameRate = FrameInterval2FrameRate(target->dwFrameInterval);
#ifdef DBUG_UVC_DEVICE
        printf("[ %s UVC_VS_COMMIT_CONTROL ]\n",__func__);
        printf("[ %s iformat %d iframe %d width %d height %d FrameRate %d ]\n",__func__,
                        uvc->set.iformat,uvc->set.iframe,uvc->set.width,
                        uvc->set.height,uvc->set.frameRate);
#endif
	 	ret = uvc_handle_streamon_event(uvc);
	    if (ret < 0)
        {
			goto err;
        }
     }

	return 0;

err:
	return ret;
}

static S8 uvc_events_process(uvc_device *uvc)
{
	struct v4l2_event v4l2_event;
	struct uvc_event *uvc_event = (struct uvc_event *)&v4l2_event.u.data;
	struct uvc_request_data resp;
    int uvc_fd = uvc->uvc_fd;
	int ret;

	ret = ioctl(uvc_fd, VIDIOC_DQEVENT, &v4l2_event);
	if (ret < 0) {
		printf("VIDIOC_DQEVENT failed: %s (%d)\n", strerror(errno),
			errno);
		return ret;
	}

	memset(&resp, 0, sizeof resp);
	resp.length = -EL2HLT;

	switch (v4l2_event.type) {
	case UVC_EVENT_CONNECT:
		return 0;

	case UVC_EVENT_DISCONNECT:
		uvc->uvc_shutdown_requested = 1;
		printf("UVC: Possible USB shutdown requested from "
				"Host, seen via UVC_EVENT_DISCONNECT\n");
		return 0;

	case UVC_EVENT_SETUP:
		uvc_events_process_setup(uvc,&uvc_event->req, &resp);
        break;

	case UVC_EVENT_DATA:
		ret = uvc_events_process_data(uvc,&uvc_event->data);
		if (ret < 0)
			break;
		return 0;

	case UVC_EVENT_STREAMON:
        /* Only Isoc mode can be here */
		uvc_handle_streamon_event(uvc);
		return 0;

	case UVC_EVENT_STREAMOFF:
		/* Stop Input streaming... */
        printf(" UVC_EVENT_STREAMOFF \n");
		if (uvc->input_video_straming) {
            uvc->set.uvc_input_StopCaputre(uvc,0);
			uvc->input_video_straming = 0;
		}

		/* ... and now UVC streaming.. */
		if (uvc->output_video_straming) {
			uvc->output_video_straming = 0;
            uvc->has_reqbufs = 0;
			uvc_video_stream(uvc_fd,0);
			uvc_uninit_device(uvc);
			uvc_video_reqbufs(uvc,0);
		}

		return 0;
	}

    ret = ioctl(uvc_fd, UVCIOC_SEND_RESPONSE, &resp);
    if (ret < 0) {
        printf("UVCIOC_S_EVENT failed: %s (%d)\n", strerror(errno),errno);
        return ret;
    }   

    return 0;
}

static S8 uvc_video_process(uvc_device *uvc)
{
	struct v4l2_buffer ubuf;
	int ret;

    int uvc_fd = uvc->uvc_fd;

	/*
	 * Return immediately if UVC video output device has not started
	 * streaming yet.
	 */
	if (!uvc->output_video_straming)
		return 0;

	/* Prepare a v4l2 buffer to be dequeued from UVC domain. */
	CLEAR(ubuf);

	ubuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	ubuf.memory = V4L2_MEMORY_MMAP;


	ret = ioctl(uvc_fd, VIDIOC_DQBUF, &ubuf);
	if (ret < 0)
    {
        printf("[ %s VIDIOC_DQBUF Fail  ret : %d ]\n",__func__,ret);
		return ret;
    }
#ifdef UVC_DEBUG_ENABLE
	printf("DeQueued buffer at UVC side = %d\n", ubuf.index);
#endif

    ret = uvc->set.uvc_fillbuf(uvc,&ubuf.bytesused,uvc->mem[ubuf.index].start);
    if( ret <0 )
    {
        printf(" %s some thing error in fill data\n",__func__);
        return ret;
    }

	ret = ioctl(uvc_fd, VIDIOC_QBUF, &ubuf);
	if (ret < 0) {
		printf("UVC: Unable to queue buffer: %s (%d).\n",
				strerror(errno), errno);
		return ret;
	}

#ifdef UVC_DEBUG_ENABLE
	printf("ReQueueing buffer at UVC side = %d\n", ubuf.index);
#endif

	return 0;
}

void uvc_process(uvc_device *uvc)
{
     int ret = -1;
     fd_set fdsu;
     int uvc_fd = uvc->uvc_fd;

     printf("\nEnter \"ctrl-c\" to exit the App\n\n");
     while(1)
     {   
        /* Exit process ? */
        if(uvc_crash)
            uvc->uvc_shutdown_requested = 1;

        if( uvc->uvc_shutdown_requested == 1 )
            break;

        FD_ZERO(&fdsu);

        /* We want both setup and data events on UVC interface.. */
        FD_SET(uvc_fd, &fdsu);
        fd_set efds = fdsu;
        fd_set dfds = fdsu;
         
        ret = select(uvc_fd + 1, NULL,
                 &dfds, &efds, NULL);

        if (-1 == ret) {
            printf("select error %d, %s\n",
                    errno, strerror (errno));
            if (EINTR == errno)
                continue;
            break;
        }

        if (0 == ret) {
            printf("select timeout\n");
            break;
        }

        if (FD_ISSET(uvc_fd, &efds))
        {
           // printf("[ %s 1 ]\n",__func__);
            uvc_events_process(uvc);
        }
        if (FD_ISSET(uvc_fd, &dfds))
        {
           // printf("[ %s 2 ]\n",__func__);
            uvc_video_process(uvc);
        }
     } 
}

void uvc_device_exit(uvc_device *uvc)
{
   printf("[ %s ]\n",__func__);
   int uvc_fd = uvc->uvc_fd;

   /* Stop Input streaming... */
   if (uvc->input_video_straming) {
        uvc->set.uvc_input_StopCaputre(uvc,0);
        uvc->input_video_straming = 0; 
   }    

   /* ... and now UVC streaming.. */
   if (uvc->output_video_straming) {
        uvc->output_video_straming = 0; 
        uvc->has_reqbufs = 0; 
        uvc_video_stream(uvc_fd,0);
        uvc_uninit_device(uvc);
        uvc_video_reqbufs(uvc,0);
   } 
}
 
static void uvc_device_int()
{
     printf("[ %s ]\n",__func__);
     uvc_crash = 1;    
}
