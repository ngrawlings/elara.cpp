#include "ElaraOpenClSurfaceWidget.h"

#include <dlfcn.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <vector>

namespace elara {

namespace {

typedef int32_t cl_int;
typedef uint32_t cl_uint;
typedef uint64_t cl_ulong;
typedef uintptr_t cl_device_type;
typedef uintptr_t cl_mem_flags;
typedef uint32_t cl_bool;
typedef intptr_t cl_context_properties;

struct _cl_platform_id;
struct _cl_device_id;
struct _cl_context;
struct _cl_command_queue;
struct _cl_program;
struct _cl_kernel;
struct _cl_mem;

typedef _cl_platform_id* cl_platform_id;
typedef _cl_device_id* cl_device_id;
typedef _cl_context* cl_context;
typedef _cl_command_queue* cl_command_queue;
typedef _cl_program* cl_program;
typedef _cl_kernel* cl_kernel;
typedef _cl_mem* cl_mem;

static const cl_int CL_SUCCESS = 0;
static const cl_bool CL_TRUE = 1;
static const cl_device_type CL_DEVICE_TYPE_CPU = (cl_device_type)(1 << 1);
static const cl_device_type CL_DEVICE_TYPE_GPU = (cl_device_type)(1 << 2);
static const cl_device_type CL_DEVICE_TYPE_ALL = 0xFFFFFFFFu;
static const cl_mem_flags CL_MEM_READ_ONLY = (cl_mem_flags)(1 << 2);
static const cl_mem_flags CL_MEM_WRITE_ONLY = (cl_mem_flags)(1 << 1);
static const cl_mem_flags CL_MEM_COPY_HOST_PTR = (cl_mem_flags)(1 << 5);
static const cl_uint CL_PROGRAM_BUILD_LOG = 0x1183;

struct OpenClSurfaceKernelCommand {
    cl_int op;
    float x0;
    float y0;
    float x1;
    float y1;
    float value0;
    float value1;
    float r;
    float g;
    float b;
};

static const char* kOpenClSurfaceKernelSource =
    "typedef struct {\n"
    "  int op;\n"
    "  float x0;\n"
    "  float y0;\n"
    "  float x1;\n"
    "  float y1;\n"
    "  float value0;\n"
    "  float value1;\n"
    "  float r;\n"
    "  float g;\n"
    "  float b;\n"
    "} SurfaceCmd;\n"
    "float clamp01(float v) {\n"
    "  return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);\n"
    "}\n"
    "float lineDistance(float2 p, float2 a, float2 b) {\n"
    "  float2 ab = b - a;\n"
    "  float len2 = dot(ab, ab);\n"
    "  if(len2 <= 0.000001f) return length(p - a);\n"
    "  float t = clamp(dot(p - a, ab) / len2, 0.0f, 1.0f);\n"
    "  float2 proj = a + (ab * t);\n"
    "  return length(p - proj);\n"
    "}\n"
    "__kernel void elara_opencl_surface(\n"
    "  __global uchar4* pixels,\n"
    "  int width,\n"
    "  int height,\n"
    "  float virtual_width,\n"
    "  float virtual_height,\n"
    "  __global const SurfaceCmd* commands,\n"
    "  int command_count\n"
    ") {\n"
    "  int gx = get_global_id(0);\n"
    "  int gy = get_global_id(1);\n"
    "  if(gx >= width || gy >= height) return;\n"
    "  float vx = ((float)gx / (float)width) * virtual_width;\n"
    "  float vy = ((float)gy / (float)height) * virtual_height;\n"
    "  float3 color = (float3)(0.10f, 0.11f, 0.14f);\n"
    "  float line_width = fmax(1.5f, virtual_width / (float)width);\n"
    "  for(int i = 0; i < command_count; i++) {\n"
    "    SurfaceCmd cmd = commands[i];\n"
    "    if(cmd.op == 0) {\n"
    "      color = (float3)(cmd.r, cmd.g, cmd.b);\n"
    "    } else if(cmd.op == 1) {\n"
    "      if(vx >= cmd.x0 && vx < (cmd.x0 + cmd.x1) && vy >= cmd.y0 && vy < (cmd.y0 + cmd.y1)) {\n"
    "        color = (float3)(cmd.r, cmd.g, cmd.b);\n"
    "      }\n"
    "    } else if(cmd.op == 2) {\n"
    "      float d = lineDistance((float2)(vx, vy), (float2)(cmd.x0, cmd.y0), (float2)(cmd.x1, cmd.y1));\n"
    "      if(d <= line_width) {\n"
    "        color = (float3)(cmd.r, cmd.g, cmd.b);\n"
    "      }\n"
    "    }\n"
    "  }\n"
    "  int index = gy * width + gx;\n"
    "  pixels[index] = (uchar4)(\n"
    "    (uchar)(clamp01(color.x) * 255.0f),\n"
    "    (uchar)(clamp01(color.y) * 255.0f),\n"
    "    (uchar)(clamp01(color.z) * 255.0f),\n"
    "    (uchar)255\n"
    "  );\n"
    "}\n";

}

class ElaraOpenClSurfaceWidget::OpenClRuntime {
private:
    void* library_handle;
    cl_platform_id platform;
    cl_device_id device;
    cl_context context;
    cl_command_queue queue;
    cl_program program;
    cl_kernel kernel;

public:
    typedef cl_int (*FnGetPlatformIDs)(cl_uint, cl_platform_id*, cl_uint*);
    typedef cl_int (*FnGetDeviceIDs)(cl_platform_id, cl_device_type, cl_uint, cl_device_id*, cl_uint*);
    typedef cl_context (*FnCreateContext)(const cl_context_properties*, cl_uint, const cl_device_id*, void*, void*, cl_int*);
    typedef cl_command_queue (*FnCreateCommandQueue)(cl_context, cl_device_id, cl_ulong, cl_int*);
    typedef cl_program (*FnCreateProgramWithSource)(cl_context, cl_uint, const char**, const size_t*, cl_int*);
    typedef cl_int (*FnBuildProgram)(cl_program, cl_uint, const cl_device_id*, const char*, void*, void*);
    typedef cl_int (*FnGetProgramBuildInfo)(cl_program, cl_device_id, cl_uint, size_t, void*, size_t*);
    typedef cl_kernel (*FnCreateKernel)(cl_program, const char*, cl_int*);
    typedef cl_mem (*FnCreateBuffer)(cl_context, cl_mem_flags, size_t, void*, cl_int*);
    typedef cl_int (*FnSetKernelArg)(cl_kernel, cl_uint, size_t, const void*);
    typedef cl_int (*FnEnqueueWriteBuffer)(cl_command_queue, cl_mem, cl_bool, size_t, size_t, const void*, cl_uint, const void*, void*);
    typedef cl_int (*FnEnqueueNDRangeKernel)(cl_command_queue, cl_kernel, cl_uint, const size_t*, const size_t*, const size_t*, cl_uint, const void*, void*);
    typedef cl_int (*FnEnqueueReadBuffer)(cl_command_queue, cl_mem, cl_bool, size_t, size_t, void*, cl_uint, const void*, void*);
    typedef cl_int (*FnFinish)(cl_command_queue);
    typedef cl_int (*FnReleaseMemObject)(cl_mem);
    typedef cl_int (*FnReleaseKernel)(cl_kernel);
    typedef cl_int (*FnReleaseProgram)(cl_program);
    typedef cl_int (*FnReleaseCommandQueue)(cl_command_queue);
    typedef cl_int (*FnReleaseContext)(cl_context);

    FnGetPlatformIDs clGetPlatformIDs;
    FnGetDeviceIDs clGetDeviceIDs;
    FnCreateContext clCreateContext;
    FnCreateCommandQueue clCreateCommandQueue;
    FnCreateProgramWithSource clCreateProgramWithSource;
    FnBuildProgram clBuildProgram;
    FnGetProgramBuildInfo clGetProgramBuildInfo;
    FnCreateKernel clCreateKernel;
    FnCreateBuffer clCreateBuffer;
    FnSetKernelArg clSetKernelArg;
    FnEnqueueWriteBuffer clEnqueueWriteBuffer;
    FnEnqueueNDRangeKernel clEnqueueNDRangeKernel;
    FnEnqueueReadBuffer clEnqueueReadBuffer;
    FnFinish clFinish;
    FnReleaseMemObject clReleaseMemObject;
    FnReleaseKernel clReleaseKernel;
    FnReleaseProgram clReleaseProgram;
    FnReleaseCommandQueue clReleaseCommandQueue;
    FnReleaseContext clReleaseContext;

    OpenClRuntime()
        : library_handle(0),
          platform(0),
          device(0),
          context(0),
          queue(0),
          program(0),
          kernel(0),
          clGetPlatformIDs(0),
          clGetDeviceIDs(0),
          clCreateContext(0),
          clCreateCommandQueue(0),
          clCreateProgramWithSource(0),
          clBuildProgram(0),
          clGetProgramBuildInfo(0),
          clCreateKernel(0),
          clCreateBuffer(0),
          clSetKernelArg(0),
          clEnqueueWriteBuffer(0),
          clEnqueueNDRangeKernel(0),
          clEnqueueReadBuffer(0),
          clFinish(0),
          clReleaseMemObject(0),
          clReleaseKernel(0),
          clReleaseProgram(0),
          clReleaseCommandQueue(0),
          clReleaseContext(0) {}

    ~OpenClRuntime() {
        if(kernel && clReleaseKernel) {
            clReleaseKernel(kernel);
        }
        if(program && clReleaseProgram) {
            clReleaseProgram(program);
        }
        if(queue && clReleaseCommandQueue) {
            clReleaseCommandQueue(queue);
        }
        if(context && clReleaseContext) {
            clReleaseContext(context);
        }
        if(library_handle) {
            dlclose(library_handle);
        }
    }

    bool load(String* status) {
        library_handle = dlopen("libOpenCL.so.1", RTLD_LAZY);
        if(!library_handle) {
            if(status) {
                *status = "OpenCL runtime library not found";
            }
            return false;
        }

#define ELARA_LOAD_CL(name, type) \
        name = (type)dlsym(library_handle, #name); \
        if(!name) { \
            if(status) { \
                *status = String("Missing OpenCL symbol: ") + #name; \
            } \
            return false; \
        }

        ELARA_LOAD_CL(clGetPlatformIDs, FnGetPlatformIDs);
        ELARA_LOAD_CL(clGetDeviceIDs, FnGetDeviceIDs);
        ELARA_LOAD_CL(clCreateContext, FnCreateContext);
        ELARA_LOAD_CL(clCreateCommandQueue, FnCreateCommandQueue);
        ELARA_LOAD_CL(clCreateProgramWithSource, FnCreateProgramWithSource);
        ELARA_LOAD_CL(clBuildProgram, FnBuildProgram);
        ELARA_LOAD_CL(clGetProgramBuildInfo, FnGetProgramBuildInfo);
        ELARA_LOAD_CL(clCreateKernel, FnCreateKernel);
        ELARA_LOAD_CL(clCreateBuffer, FnCreateBuffer);
        ELARA_LOAD_CL(clSetKernelArg, FnSetKernelArg);
        ELARA_LOAD_CL(clEnqueueWriteBuffer, FnEnqueueWriteBuffer);
        ELARA_LOAD_CL(clEnqueueNDRangeKernel, FnEnqueueNDRangeKernel);
        ELARA_LOAD_CL(clEnqueueReadBuffer, FnEnqueueReadBuffer);
        ELARA_LOAD_CL(clFinish, FnFinish);
        ELARA_LOAD_CL(clReleaseMemObject, FnReleaseMemObject);
        ELARA_LOAD_CL(clReleaseKernel, FnReleaseKernel);
        ELARA_LOAD_CL(clReleaseProgram, FnReleaseProgram);
        ELARA_LOAD_CL(clReleaseCommandQueue, FnReleaseCommandQueue);
        ELARA_LOAD_CL(clReleaseContext, FnReleaseContext);

#undef ELARA_LOAD_CL

        return true;
    }

    bool initialize(String* status) {
        cl_int rc = CL_SUCCESS;
        cl_uint platform_count = 0;

        rc = clGetPlatformIDs(0, 0, &platform_count);
        if(rc != CL_SUCCESS || platform_count == 0) {
            if(status) {
                *status = "No OpenCL platforms available";
            }
            return false;
        }

        std::vector<cl_platform_id> platforms(platform_count);
        rc = clGetPlatformIDs(platform_count, &platforms[0], 0);
        if(rc != CL_SUCCESS) {
            if(status) {
                *status = "Failed to enumerate OpenCL platforms";
            }
            return false;
        }

        for(size_t i = 0; i < platforms.size() && !device; i++) {
            cl_uint device_count = 0;
            if(clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_GPU, 0, 0, &device_count) == CL_SUCCESS && device_count > 0) {
                std::vector<cl_device_id> devices(device_count);
                if(clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_GPU, device_count, &devices[0], 0) == CL_SUCCESS) {
                    platform = platforms[i];
                    device = devices[0];
                }
            }
        }

        for(size_t i = 0; i < platforms.size() && !device; i++) {
            cl_uint device_count = 0;
            if(clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_ALL, 0, 0, &device_count) == CL_SUCCESS && device_count > 0) {
                std::vector<cl_device_id> devices(device_count);
                if(clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_ALL, device_count, &devices[0], 0) == CL_SUCCESS) {
                    platform = platforms[i];
                    device = devices[0];
                }
            }
        }

        if(!device) {
            if(status) {
                *status = "No OpenCL devices available";
            }
            return false;
        }

        context = clCreateContext(0, 1, &device, 0, 0, &rc);
        if(rc != CL_SUCCESS || !context) {
            if(status) {
                *status = "Failed to create OpenCL context";
            }
            return false;
        }

        queue = clCreateCommandQueue(context, device, 0, &rc);
        if(rc != CL_SUCCESS || !queue) {
            if(status) {
                *status = "Failed to create OpenCL command queue";
            }
            return false;
        }

        const char* source = kOpenClSurfaceKernelSource;
        program = clCreateProgramWithSource(context, 1, &source, 0, &rc);
        if(rc != CL_SUCCESS || !program) {
            if(status) {
                *status = "Failed to create OpenCL program";
            }
            return false;
        }

        rc = clBuildProgram(program, 1, &device, 0, 0, 0);
        if(rc != CL_SUCCESS) {
            if(status) {
                size_t log_size = 0;
                clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0, 0, &log_size);
                std::vector<char> log(log_size + 1, 0);
                if(log_size > 0) {
                    clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, log_size, &log[0], 0);
                }
                *status = String("OpenCL program build failed: ") + String(&log[0]);
            }
            return false;
        }

        kernel = clCreateKernel(program, "elara_opencl_surface", &rc);
        if(rc != CL_SUCCESS || !kernel) {
            if(status) {
                *status = "Failed to create OpenCL kernel";
            }
            return false;
        }

        if(status) {
            *status = "OpenCL active";
        }
        return true;
    }

    bool render(
        const std::vector<OpenClSurfaceKernelCommand>& commands,
        int width,
        int height,
        float virtual_width,
        float virtual_height,
        std::vector<unsigned char>* pixels,
        String* status
    ) {
        if(!pixels || width <= 0 || height <= 0 || !context || !queue || !kernel) {
            if(status) {
                *status = "OpenCL render path unavailable";
            }
            return false;
        }

        cl_int rc = CL_SUCCESS;
        size_t pixel_bytes = (size_t)width * (size_t)height * 4u;
        pixels->assign(pixel_bytes, 0);

        cl_mem pixel_mem = clCreateBuffer(context, CL_MEM_WRITE_ONLY, pixel_bytes, 0, &rc);
        if(rc != CL_SUCCESS || !pixel_mem) {
            if(status) {
                *status = "Failed to allocate OpenCL pixel buffer";
            }
            return false;
        }

        cl_mem command_mem = 0;
        int command_count = (int)commands.size();
        if(command_count > 0) {
            command_mem = clCreateBuffer(
                context,
                CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                sizeof(OpenClSurfaceKernelCommand) * (size_t)command_count,
                (void*)&commands[0],
                &rc
            );
            if(rc != CL_SUCCESS || !command_mem) {
                clReleaseMemObject(pixel_mem);
                if(status) {
                    *status = "Failed to allocate OpenCL command buffer";
                }
                return false;
            }
        } else {
            OpenClSurfaceKernelCommand empty_command;
            memset(&empty_command, 0, sizeof(empty_command));
            command_mem = clCreateBuffer(
                context,
                CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                sizeof(OpenClSurfaceKernelCommand),
                &empty_command,
                &rc
            );
            command_count = 1;
        }

        rc  = clSetKernelArg(kernel, 0, sizeof(cl_mem), &pixel_mem);
        rc |= clSetKernelArg(kernel, 1, sizeof(int), &width);
        rc |= clSetKernelArg(kernel, 2, sizeof(int), &height);
        rc |= clSetKernelArg(kernel, 3, sizeof(float), &virtual_width);
        rc |= clSetKernelArg(kernel, 4, sizeof(float), &virtual_height);
        rc |= clSetKernelArg(kernel, 5, sizeof(cl_mem), &command_mem);
        rc |= clSetKernelArg(kernel, 6, sizeof(int), &command_count);
        if(rc != CL_SUCCESS) {
            clReleaseMemObject(command_mem);
            clReleaseMemObject(pixel_mem);
            if(status) {
                *status = "Failed to bind OpenCL kernel arguments";
            }
            return false;
        }

        size_t global_work_size[2];
        global_work_size[0] = (size_t)width;
        global_work_size[1] = (size_t)height;

        rc = clEnqueueNDRangeKernel(queue, kernel, 2, 0, global_work_size, 0, 0, 0, 0);
        if(rc == CL_SUCCESS) {
            rc = clFinish(queue);
        }
        if(rc == CL_SUCCESS) {
            rc = clEnqueueReadBuffer(queue, pixel_mem, CL_TRUE, 0, pixel_bytes, &(*pixels)[0], 0, 0, 0);
        }

        clReleaseMemObject(command_mem);
        clReleaseMemObject(pixel_mem);

        if(rc != CL_SUCCESS) {
            if(status) {
                *status = "OpenCL kernel execution failed";
            }
            return false;
        }

        if(status) {
            *status = String("OpenCL active: ") + String(width) + "x" + String(height);
        }
        return true;
    }
};

ElaraOpenClSurfaceCommand::ElaraOpenClSurfaceCommand(Type command_type)
    : type(command_type),
      x0(0),
      y0(0),
      x1(0),
      y1(0),
      value0(0),
      value1(0),
      r(1),
      g(1),
      b(1),
      text("") {}

ElaraOpenClSurfaceWidget::ElaraOpenClSurfaceWidget(
    ElaraWidgetRegister* root_widget,
    ElaraWidgetHandle widget_handle
) : ElaraCanvasWidget(root_widget, widget_handle),
    backend_id("opencl"),
    kernel_name(""),
    overlay_text(""),
    virtual_width(1000.0),
    virtual_height(1000.0),
    execution_status("OpenCL pending"),
    opencl_runtime(0) {
    setPaletteMaster("panel");
}

ElaraOpenClSurfaceWidget::~ElaraOpenClSurfaceWidget() {
    if(opencl_runtime) {
        delete opencl_runtime;
        opencl_runtime = 0;
    }
}

double ElaraOpenClSurfaceWidget::scaleX(double value) const {
    if(virtual_width <= 0) {
        return value;
    }

    return (value / virtual_width) * (double)width;
}

double ElaraOpenClSurfaceWidget::scaleY(double value) const {
    if(virtual_height <= 0) {
        return value;
    }

    return (value / virtual_height) * (double)height;
}

void ElaraOpenClSurfaceWidget::clearCommands() {
    commands.clear();
}

void ElaraOpenClSurfaceWidget::addClear(double red, double green, double blue) {
    Ref<ElaraOpenClSurfaceCommand> cmd(new ElaraOpenClSurfaceCommand(ElaraOpenClSurfaceCommand::CLEAR));
    cmd->r = red;
    cmd->g = green;
    cmd->b = blue;
    commands.push(cmd);
}

void ElaraOpenClSurfaceWidget::addRect(
    double x,
    double y,
    double w,
    double h,
    double red,
    double green,
    double blue
) {
    Ref<ElaraOpenClSurfaceCommand> cmd(new ElaraOpenClSurfaceCommand(ElaraOpenClSurfaceCommand::RECT));
    cmd->x0 = x;
    cmd->y0 = y;
    cmd->x1 = w;
    cmd->y1 = h;
    cmd->r = red;
    cmd->g = green;
    cmd->b = blue;
    commands.push(cmd);
}

void ElaraOpenClSurfaceWidget::addLine(
    double x0,
    double y0,
    double x1,
    double y1,
    double red,
    double green,
    double blue
) {
    Ref<ElaraOpenClSurfaceCommand> cmd(new ElaraOpenClSurfaceCommand(ElaraOpenClSurfaceCommand::LINE));
    cmd->x0 = x0;
    cmd->y0 = y0;
    cmd->x1 = x1;
    cmd->y1 = y1;
    cmd->r = red;
    cmd->g = green;
    cmd->b = blue;
    commands.push(cmd);
}

void ElaraOpenClSurfaceWidget::addText(
    double x,
    double y,
    const String& value,
    double size,
    double red,
    double green,
    double blue
) {
    Ref<ElaraOpenClSurfaceCommand> cmd(new ElaraOpenClSurfaceCommand(ElaraOpenClSurfaceCommand::TEXT));
    cmd->x0 = x;
    cmd->y0 = y;
    cmd->value0 = size;
    cmd->text = value;
    cmd->r = red;
    cmd->g = green;
    cmd->b = blue;
    commands.push(cmd);
}

void ElaraOpenClSurfaceWidget::setBackendId(const String& value) {
    backend_id = value;
}

String ElaraOpenClSurfaceWidget::getBackendId() const {
    return backend_id;
}

void ElaraOpenClSurfaceWidget::setKernelName(const String& value) {
    kernel_name = value;
}

String ElaraOpenClSurfaceWidget::getKernelName() const {
    return kernel_name;
}

void ElaraOpenClSurfaceWidget::setOverlayText(const String& value) {
    overlay_text = value;
}

String ElaraOpenClSurfaceWidget::getOverlayText() const {
    return overlay_text;
}

void ElaraOpenClSurfaceWidget::setVirtualSize(double width_value, double height_value) {
    virtual_width = width_value > 0 ? width_value : 1000.0;
    virtual_height = height_value > 0 ? height_value : 1000.0;
}

double ElaraOpenClSurfaceWidget::getVirtualWidth() const {
    return virtual_width;
}

double ElaraOpenClSurfaceWidget::getVirtualHeight() const {
    return virtual_height;
}

void ElaraOpenClSurfaceWidget::drawEmptyState(ElaraDrawContext* ctx) {
    ctx->setColor(0.15, 0.17, 0.20);
    ctx->fillRect(0, 0, width, height);

    ctx->setColor(0.25, 0.28, 0.32);
    for(int x = 0; x < width; x += 24) {
        ctx->line(x, 0, x, height, 1.0);
    }
    for(int y = 0; y < height; y += 24) {
        ctx->line(0, y, width, y, 1.0);
    }

    ctx->setColor(0.94, 0.56, 0.10);
    ctx->drawText(16, 26, "OpenCL Surface", 16);

    ctx->setColor(0.82, 0.84, 0.88);
    ctx->drawText(16, 46, String("backend: ") + backend_id, 12);

    if(kernel_name.length() > 0) {
        ctx->drawText(16, 64, String("kernel: ") + kernel_name, 12);
    }

    if(overlay_text.length() > 0) {
        ctx->drawText(16, 86, overlay_text, 12);
    } else {
        ctx->drawText(16, 86, "Awaiting root-node scene commands", 12);
    }

    if(execution_status.length() > 0) {
        ctx->drawText(16, 106, execution_status, 12);
    }
}

bool ElaraOpenClSurfaceWidget::renderOpenCl(int pixel_width, int pixel_height) {
    if(pixel_width <= 0 || pixel_height <= 0) {
        execution_status = "OpenCL skipped: invalid surface size";
        return false;
    }

    if(!opencl_runtime) {
        opencl_runtime = new OpenClRuntime();
        if(!opencl_runtime->load(&execution_status)) {
            return false;
        }
        if(!opencl_runtime->initialize(&execution_status)) {
            return false;
        }
    }

    std::vector<OpenClSurfaceKernelCommand> kernel_commands;
    for(int i = 0; i < (int)commands.length(); i++) {
        Ref<ElaraOpenClSurfaceCommand> cmd = commands[i];
        if(!cmd) {
            continue;
        }

        if(cmd->type == ElaraOpenClSurfaceCommand::TEXT) {
            continue;
        }

        OpenClSurfaceKernelCommand kernel_cmd;
        memset(&kernel_cmd, 0, sizeof(kernel_cmd));
        kernel_cmd.op = (cl_int)cmd->type;
        kernel_cmd.x0 = (float)cmd->x0;
        kernel_cmd.y0 = (float)cmd->y0;
        kernel_cmd.x1 = (float)cmd->x1;
        kernel_cmd.y1 = (float)cmd->y1;
        kernel_cmd.value0 = (float)cmd->value0;
        kernel_cmd.value1 = (float)cmd->value1;
        kernel_cmd.r = (float)cmd->r;
        kernel_cmd.g = (float)cmd->g;
        kernel_cmd.b = (float)cmd->b;
        kernel_commands.push_back(kernel_cmd);
    }

    return opencl_runtime->render(
        kernel_commands,
        pixel_width,
        pixel_height,
        (float)virtual_width,
        (float)virtual_height,
        &pixel_buffer,
        &execution_status
    );
}

void ElaraOpenClSurfaceWidget::drawCanvas(ElaraDrawContext* ctx) {
    if(commands.length() <= 0) {
        drawEmptyState(ctx);
        return;
    }

    if(renderOpenCl((int)width, (int)height) && !pixel_buffer.empty()) {
        ctx->drawBitmapRgba(0, 0, (int)width, (int)height, &pixel_buffer[0], (int)width * 4);
    } else {
        drawEmptyState(ctx);
    }

    for(int i = 0; i < (int)commands.length(); i++) {
        Ref<ElaraOpenClSurfaceCommand> cmd = commands[i];
        if(!cmd) {
            continue;
        }

        if(cmd->type == ElaraOpenClSurfaceCommand::TEXT) {
            ctx->setColor(cmd->r, cmd->g, cmd->b);
            ctx->drawText(scaleX(cmd->x0), scaleY(cmd->y0), cmd->text, scaleY(cmd->value0));
        }
    }

    ctx->setColor(0.95, 0.96, 0.98);
    if(overlay_text.length() > 0) {
        ctx->drawText(14, 22, overlay_text, 12);
    }

    if(execution_status.length() > 0) {
        ctx->drawText(14, 40, execution_status, 12);
    }
}

}
