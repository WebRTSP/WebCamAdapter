#include <optional>
#include <string>
#include <filesystem>
#include <fstream>
#include <iostream>

#include <linux/usb/ch9.h>

#include <glib.h>

#include <usbg/usbg.h>
#include <usbg/function/uvc.h>

extern "C" {
    // why they are missing in usbg/function/uvc.h??
    int usbg_f_uvc_get_config_attrs(
        usbg_f_uvc*,
        struct usbg_f_uvc_config_attrs*);
    int usbg_f_uvc_set_config_attr_val(
        usbg_f_uvc*,
        usbg_f_uvc_config_attr,
        usbg_f_uvc_config_attr_val);
}

namespace
{
    const char *const CONFIG_FS_PATH = "/sys/kernel/config";
    const char *const UDC_ENUM_PATH = "/sys/class/udc";
    const char *const V4L_DEVICE_ENUM_PATH = "/sys/class/video4linux";
    const char *const DEV_PATH = "/dev";

    const unsigned short USB_2_0 = 0x200;
    const unsigned char USB_HIGH_SPEED_MAX_PACKET_SIZE0 = 64; // USB Specification, Revision 2.0

    const unsigned short DEVICE_VID = 0x0525;
    const unsigned short DEVICE_PID = 0xa4a2;
    const unsigned short DEVICE_REVISION = 1;

    const char *const DEVICE_MANUFACTURER = "RSATom";
    const char *const DEVICE_PRODUCT = "WebCam Adapter";
    const char *const DEVICE_SERIAL = "1";

    const char *const UVC_GADGET_NAME = "wca";
    const char *const UVC_GADGET_FUNCTION_INSTANCE = "1";
    const char *const UVC_GADGET_CONFIG_FUNCTION_NAME = "function";
    const unsigned UVC_GADGET_CONFIGURATION_ID = 1;
    const char *const UVC_GADGET_CONFIGURATION_NAME = "c";
    const char *const UVC_GADGET_CONFIGURATION_DESCRIPTION = "";

    // USB Specification, Revision 2.0.
    // bmAttributes:
    // D7: Reserved (set to one)
    // D6: Self-powered
    // D5: Remote Wakeup
    // D4...0: Reserved (reset to zero)
    const unsigned char UVC_GADGET_BM_ATTRIBUTES = 0x80;
    // bMaxPower:
    // Expressed in 2 mA units
    const unsigned char UVC_GADGET_MAX_POWER = 250; // 500 mA, FIXME?

    const int UVC_GADGET_MAX_PACKET = 2048;

    #define UVC_DEC_ATTR(_name) \
        { .offset = offsetof(struct usbg_f_uvc_config_attrs, _name), }

    #define UVC_STRING_ATTR(_name) \
        { .offset = offsetof(struct usbg_f_uvc_config_attrs, _name), }

    struct {
        size_t offset;
    } uvc_config_attr[USBG_F_UVC_CONFIG_ATTR_MAX] = {
        [USBG_F_UVC_CONFIG_MAXBURST] = UVC_DEC_ATTR(streaming_maxburst),
        [USBG_F_UVC_CONFIG_MAXPACKET] = UVC_DEC_ATTR(streaming_maxpacket),
        [USBG_F_UVC_CONFIG_INTERVAL] = UVC_DEC_ATTR(streaming_interval),
        [USBG_F_UVC_CONFIG_FUNCTION_NAME] = UVC_STRING_ATTR(function_name),
    };

    #undef UVC_DEC_ATTR
    #undef UVC_STRING_ATTR

    // usbg_f_uvc_set_config_attrs is broken inside libusbgx:
    // https://github.com/linux-usb-gadgets/libusbgx/issues/110
    int usbg_f_uvc_set_config_attrs_fixed(
        usbg_f_uvc* uvcf,
        const struct usbg_f_uvc_config_attrs* iattrs)
    {
        int i;
        int ret = 0;

        for (i = USBG_F_UVC_CONFIG_ATTR_MIN; i < USBG_F_UVC_CONFIG_ATTR_MAX; ++i) {
            ret = usbg_f_uvc_set_config_attr_val(
                uvcf,
                (usbg_f_uvc_config_attr)i,
                *(union usbg_f_uvc_config_attr_val *)
                ((char *)iattrs
                + uvc_config_attr[i].offset));
            if (ret)
                break;
        }

        return ret;
    }
}

namespace fs = std::filesystem;

G_DEFINE_AUTOPTR_CLEANUP_FUNC(usbg_state, usbg_cleanup)


bool ConfigureUvcGadget() noexcept
{
    g_autoptr(usbg_state) usbg = nullptr;
    if(usbg_init(CONFIG_FS_PATH, &usbg) != USBG_SUCCESS)
        return false;
        
    usbg_gadget_attrs deviceAattributes = {
        .bcdUSB = USB_2_0,
        .bDeviceClass = USB_CLASS_PER_INTERFACE, // https://www.usb.org/defined-class-codes#anchor_BaseClass00h
        .bDeviceSubClass = 0x0,
        .bDeviceProtocol = 0x0,
        .bMaxPacketSize0 = USB_HIGH_SPEED_MAX_PACKET_SIZE0,
        .idVendor = DEVICE_VID,
        .idProduct = DEVICE_PID,
        .bcdDevice = DEVICE_REVISION,
    };

    usbg_gadget_strs deviceStrings = {
        .manufacturer = (char*)DEVICE_MANUFACTURER,
        .product = (char*)DEVICE_PRODUCT,
        .serial = (char*)DEVICE_SERIAL,
    };

    int usbgRet;

    usbg_gadget* gadget = nullptr;
    if((usbgRet = usbg_create_gadget(
        usbg,
        UVC_GADGET_NAME,
        &deviceAattributes,
        &deviceStrings,
        &gadget)) != USBG_SUCCESS)
    {
        return false;
    }

    usbg_f_uvc_frame_attrs frame1280x720x15fps = {
        .bFrameIndex = 1,
        // .bmCapabilities = ,
        // .dwMinBitRate = ,
        // .dwMaxBitRate = ,
        // .dwMaxVideoFrameBufferSize = ,
        // .dwDefaultFrameInterval = ,
        .dwFrameInterval = 666666, // 1_000_000_000ns / 100 / 15
        .wWidth = 720,
        .wHeight = 1280,
    };

    usbg_f_uvc_frame_attrs *mjpegFrameAttributes[] = {
        &frame1280x720x15fps,
        NULL,
    };

    usbg_f_uvc_format_attrs mjpegAttributes = {
        // .bmaControls = ,
        // .bFormatIndex = ,
        .bDefaultFrameIndex = 1,
        // .bAspectRatioX = ,
        // .bAspectRatioY = ,
        // .bmInterlaceFlags = ,
        .format = "mjpeg/m",
        // .bBitsPerPixel = ,
        // .guidFormat = ,
        .frames = mjpegFrameAttributes,
    };

    usbg_f_uvc_format_attrs *formats[] = {
        &mjpegAttributes,
        NULL,
    };

    usbg_f_uvc_attrs functionAttributes = {
        .formats = formats,
    };

    usbg_function* function = nullptr;
    if((usbgRet = usbg_create_function(
        gadget,
        USBG_F_UVC,
        UVC_GADGET_FUNCTION_INSTANCE,
        &functionAttributes,
        &function)) != USBG_SUCCESS)
    {
        return false;
    }

    usbg_f_uvc* uvcFunction = usbg_to_uvc_function(function);

    usbg_f_uvc_config_attrs uvcFunctionAttrs = {};
    if((usbgRet = usbg_f_uvc_get_config_attrs(
        uvcFunction,
        &uvcFunctionAttrs)) != USBG_SUCCESS)
    {
        return false;
    }

    uvcFunctionAttrs.streaming_maxpacket = UVC_GADGET_MAX_PACKET;

    if((usbgRet = usbg_f_uvc_set_config_attrs_fixed(
        uvcFunction,
        &uvcFunctionAttrs)) != USBG_SUCCESS)
    {
        return false;
    }

    usbg_config_attrs configAttributes = {
        .bmAttributes = UVC_GADGET_BM_ATTRIBUTES,
        .bMaxPower = UVC_GADGET_MAX_POWER,
    };

    usbg_config_strs configStrings = {
	    .configuration = (char*)UVC_GADGET_CONFIGURATION_DESCRIPTION,
    };

    usbg_config* config = nullptr;
    if((usbgRet = usbg_create_config(
        gadget,
        UVC_GADGET_CONFIGURATION_ID,
        UVC_GADGET_CONFIGURATION_NAME,
        &configAttributes,
        &configStrings,
        &config)) != USBG_SUCCESS)
    {
        return false;
    }

    if((usbgRet = usbg_add_config_function(
        config,
        UVC_GADGET_CONFIG_FUNCTION_NAME,
        function)) != USBG_SUCCESS)
    {
        return false;
    }

    if((usbgRet = usbg_enable_gadget(gadget, DEFAULT_UDC)) != USBG_SUCCESS) {
        return false;
    }

    //usbg_get_first_udc, usbg_get_udc_name

    return true;
}

std::optional<std::string> GetUDC() noexcept
{
    try {
        if(!fs::is_directory(UDC_ENUM_PATH))
            return {};

        for(const auto& entry: fs::directory_iterator(UDC_ENUM_PATH)) {
            if(entry.is_directory())
                return entry.path().filename();
        }
    } catch(...) {}

    return {};
}

std::optional<fs::path> GetUvcGadgetDevicePath() noexcept
{
    const std::optional<std::string> optUdc = GetUDC();
    if(!optUdc.has_value())
        return {};

    const std::string udc = optUdc.value();

    try {
        if(!fs::is_directory(V4L_DEVICE_ENUM_PATH))
            return {};

        for(const auto& entry: fs::directory_iterator(V4L_DEVICE_ENUM_PATH)) {
            if(entry.is_directory()) {
                const fs::path namePath = entry.path() / "name";
                if(fs::is_regular_file(namePath)) {
                    std::ifstream nameFile(namePath);
                    if(!nameFile.is_open())
                        continue;

                    std::string name;
                    std::getline(nameFile, name);
                    if(udc == name)
                        return DEV_PATH / entry.path().filename();
                }
            }
        }
    } catch(...) {}

    return {};
}

int main(int argc, char *argv[])
{
    ConfigureUvcGadget();

    return 0;

    g_autoptr(GMainLoop) loop = g_main_loop_new(nullptr, FALSE);

    g_main_loop_run(loop);

    return 0;
}
