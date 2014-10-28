#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <node.h>
#include <v8.h>
#include <portaudio.h>
#include <uv.h>
#include <sys/time.h>

using namespace std;
using namespace v8;
using namespace node;


extern "C" {
  #define SAMPLING_RATE 8000
  #include "dtmf.h"
}

struct AsyncBridge {
  uv_async_t handler;
  Persistent<Function> reporter;
  char code;
  PaTime lastDetection = 0;
  PaTime currentTime;
  char lastCode = NO_CODE;
};

AsyncBridge *bridge;

const PaStreamInfo* info;
const PaDeviceInfo* deviceInfo;

PaStream *stream;

static PaTime timeOut = .050; // default to 10 millisecond timeout


static double getTime() {
  struct timeval _time;
  long millis;
  gettimeofday(&_time, NULL);
  millis = ((_time.tv_sec) * 1000 + _time.tv_usec/1000.0) + 0.5;
  return millis / 1000.0;
}

static int audioCallback( const void *inputBuffer, void *outputBuffer,
                          unsigned long framesPerBuffer,
                          const PaStreamCallbackTimeInfo* timeInfo,
                          PaStreamCallbackFlags statusFlags,
                          void *userData )
{
  AsyncBridge *bridge = static_cast<AsyncBridge *>(userData);

  (void) outputBuffer;
  
  static char code = NO_CODE;
  
  DTMFDecode(inputBuffer,framesPerBuffer,&code);

  if (bridge == NULL || code == NO_CODE) return 0;

  double now = getTime();
  if (bridge->lastDetection == 0) bridge->lastDetection = now;

  PaTime duration = now - bridge->lastDetection;

  if (duration > 0.050)
  {
    bridge->lastDetection = now;
    if (duration < 0.100) {
      bridge->code = code;
      bridge->currentTime = timeInfo->currentTime;
      uv_async_send(&bridge->handler);
    }
  }
  return 0;
}


void reportCode(uv_async_t *handle, int status /*UNUSED*/) {
  TryCatch try_catch;
  AsyncBridge *bridge = static_cast<AsyncBridge*>(handle->data);

  const unsigned int argc = 2;

  Handle<Value> argv[argc] = {
    String::New(&bridge->code,1),
    Number::New(bridge->currentTime) };

    fprintf(stderr, "%f\n", bridge->currentTime);

  bridge->reporter->Call(Context::GetCurrent()->Global(), argc, argv);

  if (try_catch.HasCaught()) {
    FatalException(try_catch);
  }
}

Handle<Value> openDevice(const Arguments& args) {
  HandleScope scope;
  int device = args[0]->IntegerValue();

  PaError err;
  err = Pa_Initialize();
  if( err != paNoError ) return scope.Close(Number::New(err));

  DTMFSetup(SAMPLING_RATE, BUFFER_SIZE);
  deviceInfo = Pa_GetDeviceInfo((device == -1) ? Pa_GetDefaultInputDevice() : device);
  
  Handle<Function> callback = Handle<Function>::Cast(args[1]);
  
  if (bridge != NULL) {
    uv_close((uv_handle_t*) &bridge->handler, NULL);
    bridge->reporter.Dispose();
    delete bridge;
  }

  bridge = new AsyncBridge;
  uv_async_init(uv_default_loop(), &bridge->handler, reportCode);
  bridge->reporter = Persistent<Function>::New(callback);
  bridge->handler.data = bridge;

  if (stream != NULL) {
    PaError err = Pa_StopStream( stream );
    if( err == paNoError ) {
      err = Pa_CloseStream( stream );
    }
  }

  /* Open an audio I/O stream. */
  err = Pa_OpenDefaultStream( &stream, 1, 0,
                             paInt16,
                             SAMPLING_RATE,
                             BUFFER_SIZE,
                             audioCallback,
                             bridge );

  if( err != paNoError ) return scope.Close(Number::New(err));
  
  err = Pa_StartStream( stream );
  if( err != paNoError ) return scope.Close(Number::New(err));
  
  info = Pa_GetStreamInfo(stream);
  
  Local<Object> infoOut = Object::New();

  infoOut->Set(String::NewSymbol("Device"),String::New(deviceInfo->name));
  infoOut->Set(String::NewSymbol("Latency"),Number::New(info->inputLatency));
  infoOut->Set(String::NewSymbol("Samplerate"),Number::New(info->sampleRate));
  infoOut->Set(String::NewSymbol("Buffer Size"),Number::New(BUFFER_SIZE));

  return scope.Close(infoOut);
}

Handle<Value> closeDevice(const Arguments& args) {
  HandleScope scope;
  PaError err = Pa_StopStream( stream );
  if( err == paNoError ) {
    err = Pa_CloseStream( stream );
  }
  
  if( err != paNoError ) {
    // Close Audio:
    err = Pa_Terminate();
  }

  if(bridge!=NULL) {
    // cleanup
    uv_close((uv_handle_t*) &bridge->handler, NULL);
    bridge->reporter.Dispose();
    delete bridge;
  }

  return scope.Close(Number::New(err));
}

Handle<Value> listDevices(const Arguments& args) {
  HandleScope scope;
  PaError err;
  vector<string> devices;
  err = Pa_Initialize();
  if(err == paNoError) {
    int count = Pa_GetDeviceCount();
    for (int id = 0; id<count; id++) {
      const PaDeviceInfo *info = Pa_GetDeviceInfo(id);
      if (info->maxInputChannels > 0) {
        devices.push_back(info->name);
      }
    }
  }
  Pa_Terminate();
  Handle<Array> deviceArray = Array::New( devices.size() );
  if(err == paNoError) {
    for (unsigned int i = 0; i<devices.size(); i++) {
      deviceArray->Set( Number::New(i), String::New(&devices[i][0], devices[i].size()) );
    }
  }
  return scope.Close(deviceArray);
}

void init(Handle<Object> exports, Handle<Object> module) {
  NODE_SET_METHOD(exports, "list", listDevices);
  NODE_SET_METHOD(exports, "open", openDevice);
  NODE_SET_METHOD(exports, "close", closeDevice);
}

NODE_MODULE(dtmfin, init);