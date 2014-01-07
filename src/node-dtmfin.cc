#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <node.h>
#include <v8.h>
#include <portaudio.h>
#include <uv.h>

using namespace std;
using namespace v8;
using namespace node;

#define SAMPLING_RATE 8000

extern "C" {
  #include "dtmf.h"
}

struct async_req {
  uv_work_t req;
  char code;
  Persistent<Function> callback;
};

void DoAsync (uv_work_t *r);
void AfterDetect (uv_work_t *r);

async_req *req;

static PaTime timeOut = .5; // default to 200 millisecond timeout

static int audioCallback( const void *inputBuffer, void *outputBuffer,
                          unsigned long framesPerBuffer,
                          const PaStreamCallbackTimeInfo* timeInfo,
                          PaStreamCallbackFlags statusFlags,
                          void *userData )
{
  async_req *req = reinterpret_cast<async_req *>(userData);

  static PaTime lastDetection = timeInfo->currentTime;
  static char lastCode = NO_CODE;
  
  (void) outputBuffer;
  
  static char code;
  
  DTMFDecode(inputBuffer,framesPerBuffer,&code);
  
  if(code != NO_CODE && (code!=lastCode || (code == lastCode && timeInfo->currentTime-lastDetection > timeOut)))
  {
    lastDetection = timeInfo->currentTime;
    lastCode = code;
    if (userData != NULL) {
      req->code = code;
      uv_queue_work(uv_default_loop(),
                    &req->req,
                    DoAsync,
                    (uv_after_work_cb)AfterDetect);
    }
  }



  return 0;
}

void AfterDetect (uv_work_t *r) {
  HandleScope scope;
  async_req *req = reinterpret_cast<async_req *>(r->data);

  Handle<Value> argv[1] = { String::New(&req->code,1) };

  TryCatch try_catch;

  req->callback->Call(Context::GetCurrent()->Global(), 1, argv);

  if (try_catch.HasCaught()) {
    FatalException(try_catch);
  }
}

void DoAsync (uv_work_t *r) {
}


const PaStreamInfo* info;
const PaDeviceInfo* deviceInfo;

PaStream *stream;

Handle<Value> openDevice(const Arguments& args) {
  HandleScope scope;
  int device = args[0]->IntegerValue();

  PaError err;
  err = Pa_Initialize();
  if( err != paNoError ) return scope.Close(Number::New(err));

  DTMFSetup(SAMPLING_RATE, BUFFER_SIZE);
  deviceInfo = Pa_GetDeviceInfo((device == -1) ? Pa_GetDefaultInputDevice() : device);
  
  if (req != NULL) {
    req->callback.Dispose();
    delete req;
  }

  req = new async_req;
  req->req.data = req;

  Local<Function> callback = Local<Function>::Cast(args[1]);
  req->callback = Persistent<Function>::New(callback);
  
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
                             req );

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

  if(req!=NULL) {
    // cleanup
    req->callback.Dispose();
    delete req;
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
    for (int i = 0; i<devices.size(); i++) {
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