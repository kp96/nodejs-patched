#include "node_internals.h"
#include "node_watchdog.h"

namespace node {
namespace util {

using v8::Array;
using v8::Context;
using v8::FunctionCallbackInfo;
using v8::Integer;
using v8::Local;
using v8::Maybe;
using v8::Object;
using v8::Private;
using v8::Promise;
using v8::Proxy;
using v8::Value;


#define VALUE_METHOD_MAP(V)                                                   \
  V(isArrayBuffer, IsArrayBuffer)                                             \
  V(isArrayBufferView, IsArrayBufferView)                                     \
  V(isAsyncFunction, IsAsyncFunction)                                         \
  V(isDataView, IsDataView)                                                   \
  V(isDate, IsDate)                                                           \
  V(isExternal, IsExternal)                                                   \
  V(isMap, IsMap)                                                             \
  V(isMapIterator, IsMapIterator)                                             \
  V(isNativeError, IsNativeError)                                             \
  V(isPromise, IsPromise)                                                     \
  V(isRegExp, IsRegExp)                                                       \
  V(isSet, IsSet)                                                             \
  V(isSetIterator, IsSetIterator)                                             \
  V(isTypedArray, IsTypedArray)                                               \
  V(isUint8Array, IsUint8Array)


#define V(_, ucname) \
  static void ucname(const FunctionCallbackInfo<Value>& args) {               \
    CHECK_EQ(1, args.Length());                                               \
    args.GetReturnValue().Set(args[0]->ucname());                             \
  }

  VALUE_METHOD_MAP(V)
#undef V

static void IsAnyArrayBuffer(const FunctionCallbackInfo<Value>& args) {
  CHECK_EQ(1, args.Length());
  args.GetReturnValue().Set(
    args[0]->IsArrayBuffer() || args[0]->IsSharedArrayBuffer());
}

static void GetPromiseDetails(const FunctionCallbackInfo<Value>& args) {
  // Return undefined if it's not a Promise.
  if (!args[0]->IsPromise())
    return;

  auto isolate = args.GetIsolate();

  Local<Promise> promise = args[0].As<Promise>();
  Local<Array> ret = Array::New(isolate, 2);

  int state = promise->State();
  ret->Set(0, Integer::New(isolate, state));
  if (state != Promise::PromiseState::kPending)
    ret->Set(1, promise->Result());

  args.GetReturnValue().Set(ret);
}

static void GetProxyDetails(const FunctionCallbackInfo<Value>& args) {
  // Return undefined if it's not a proxy.
  if (!args[0]->IsProxy())
    return;

  Local<Proxy> proxy = args[0].As<Proxy>();

  Local<Array> ret = Array::New(args.GetIsolate(), 2);
  ret->Set(0, proxy->GetTarget());
  ret->Set(1, proxy->GetHandler());

  args.GetReturnValue().Set(ret);
}

// Side effect-free stringification that will never throw exceptions.
static void SafeToString(const FunctionCallbackInfo<Value>& args) {
  auto context = args.GetIsolate()->GetCurrentContext();
  args.GetReturnValue().Set(args[0]->ToDetailString(context).ToLocalChecked());
}

inline Local<Private> IndexToPrivateSymbol(Environment* env, uint32_t index) {
#define V(name, _) &Environment::name,
  static Local<Private> (Environment::*const methods[])() const = {
    PER_ISOLATE_PRIVATE_SYMBOL_PROPERTIES(V)
  };
#undef V
  CHECK_LT(index, arraysize(methods));
  return (env->*methods[index])();
}

static void GetHiddenValue(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  CHECK(args[0]->IsObject());
  CHECK(args[1]->IsUint32());

  Local<Object> obj = args[0].As<Object>();
  auto index = args[1]->Uint32Value(env->context()).FromJust();
  auto private_symbol = IndexToPrivateSymbol(env, index);
  auto maybe_value = obj->GetPrivate(env->context(), private_symbol);

  args.GetReturnValue().Set(maybe_value.ToLocalChecked());
}

static void SetHiddenValue(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  CHECK(args[0]->IsObject());
  CHECK(args[1]->IsUint32());

  Local<Object> obj = args[0].As<Object>();
  auto index = args[1]->Uint32Value(env->context()).FromJust();
  auto private_symbol = IndexToPrivateSymbol(env, index);
  auto maybe_value = obj->SetPrivate(env->context(), private_symbol, args[2]);

  args.GetReturnValue().Set(maybe_value.FromJust());
}


void StartSigintWatchdog(const FunctionCallbackInfo<Value>& args) {
  int ret = SigintWatchdogHelper::GetInstance()->Start();
  args.GetReturnValue().Set(ret == 0);
}


void StopSigintWatchdog(const FunctionCallbackInfo<Value>& args) {
  bool had_pending_signals = SigintWatchdogHelper::GetInstance()->Stop();
  args.GetReturnValue().Set(had_pending_signals);
}


void WatchdogHasPendingSigint(const FunctionCallbackInfo<Value>& args) {
  bool ret = SigintWatchdogHelper::GetInstance()->HasPendingSignal();
  args.GetReturnValue().Set(ret);
}


void CreatePromise(const FunctionCallbackInfo<Value>& args) {
  Local<Context> context = args.GetIsolate()->GetCurrentContext();
  auto maybe_resolver = Promise::Resolver::New(context);
  if (!maybe_resolver.IsEmpty())
    args.GetReturnValue().Set(maybe_resolver.ToLocalChecked());
}


void PromiseResolve(const FunctionCallbackInfo<Value>& args) {
  Local<Context> context = args.GetIsolate()->GetCurrentContext();
  Local<Value> promise = args[0];
  CHECK(promise->IsPromise());
  if (promise.As<Promise>()->State() != Promise::kPending) return;
  Local<Promise::Resolver> resolver = promise.As<Promise::Resolver>();  // sic
  Maybe<bool> ret = resolver->Resolve(context, args[1]);
  args.GetReturnValue().Set(ret.FromMaybe(false));
}


void PromiseReject(const FunctionCallbackInfo<Value>& args) {
  Local<Context> context = args.GetIsolate()->GetCurrentContext();
  Local<Value> promise = args[0];
  CHECK(promise->IsPromise());
  if (promise.As<Promise>()->State() != Promise::kPending) return;
  Local<Promise::Resolver> resolver = promise.As<Promise::Resolver>();  // sic
  Maybe<bool> ret = resolver->Reject(context, args[1]);
  args.GetReturnValue().Set(ret.FromMaybe(false));
}

// start custom code here - kp96
// taken from https://groups.google.com/forum/#!topic/v8-users/uenzq5zSnJw
uint64_t GetTypeFlags(const v8::Local<v8::Value>& v)
{
    uint64_t result = 0;
    if (v->IsArgumentsObject()   ) result |= 0x0000000000000001;
    if (v->IsArrayBuffer()       ) result |= 0x0000000000000002;
    if (v->IsArrayBufferView()   ) result |= 0x0000000000000004;
    if (v->IsArray()             ) result |= 0x0000000000000008;
    if (v->IsBooleanObject()     ) result |= 0x0000000000000010;
    if (v->IsBoolean()           ) result |= 0x0000000000000020;
    if (v->IsDataView()          ) result |= 0x0000000000000040;
    if (v->IsDate()              ) result |= 0x0000000000000080;
    if (v->IsExternal()          ) result |= 0x0000000000000100;
    if (v->IsFalse()             ) result |= 0x0000000000000200;
    if (v->IsFloat32Array()      ) result |= 0x0000000000000400;
    if (v->IsFloat64Array()      ) result |= 0x0000000000000800;
    if (v->IsFunction()          ) result |= 0x0000000000001000;
    if (v->IsGeneratorFunction() ) result |= 0x0000000000002000;
    if (v->IsGeneratorObject()   ) result |= 0x0000000000004000;
    if (v->IsInt16Array()        ) result |= 0x0000000000008000;
    if (v->IsInt32Array()        ) result |= 0x0000000000010000;
    if (v->IsInt32()             ) result |= 0x0000000000020000;
    if (v->IsInt8Array()         ) result |= 0x0000000000040000;
    if (v->IsMapIterator()       ) result |= 0x0000000000080000;
    if (v->IsMap()               ) result |= 0x0000000000100000;
    if (v->IsName()              ) result |= 0x0000000000200000;
    if (v->IsNativeError()       ) result |= 0x0000000000400000;
    if (v->IsNull()              ) result |= 0x0000000000800000;
    if (v->IsNumberObject()      ) result |= 0x0000000001000000;
    if (v->IsNumber()            ) result |= 0x0000000002000000;
    if (v->IsObject()            ) result |= 0x0000000004000000;
    if (v->IsPromise()           ) result |= 0x0000000008000000;
    if (v->IsRegExp()            ) result |= 0x0000000010000000;
    if (v->IsSetIterator()       ) result |= 0x0000000020000000;
    if (v->IsSet()               ) result |= 0x0000000040000000;
    if (v->IsStringObject()      ) result |= 0x0000000080000000;
    if (v->IsString()            ) result |= 0x0000000100000000;
    if (v->IsSymbolObject()      ) result |= 0x0000000200000000;
    if (v->IsSymbol()            ) result |= 0x0000000400000000;
    if (v->IsTrue()              ) result |= 0x0000000800000000;
    if (v->IsTypedArray()        ) result |= 0x0000001000000000;
    if (v->IsUint16Array()       ) result |= 0x0000002000000000;
    if (v->IsUint32Array()       ) result |= 0x0000004000000000;
    if (v->IsUint32()            ) result |= 0x0000008000000000;
    if (v->IsUint8Array()        ) result |= 0x0000010000000000;
    if (v->IsUint8ClampedArray() ) result |= 0x0000020000000000;
    if (v->IsUndefined()         ) result |= 0x0000040000000000;
    if (v->IsWeakMap()           ) result |= 0x0000080000000000;
    if (v->IsWeakSet()           ) result |= 0x0000100000000000;
    return result;
}

void GetFunctionArgTypes(const FunctionCallbackInfo<Value>& args) {
  Local<Context> context = args.GetIsolate()->GetCurrentContext();
  Local<Array> ret = Array::New(args.GetIsolate(), args.Length());
  
  for (int i = 0; i < args.Length(); ++i) {
    Local<Value> v = args[i];
    uint64_t typeValue = GetTypeFlags(v);
    ret->Set(i, Integer::NewFromUnsigned(args.GetIsolate(), typeValue));
  }

  args.GetReturnValue().Set(ret);
}
// end custom code here

void Initialize(Local<Object> target,
                Local<Value> unused,
                Local<Context> context) {
  Environment* env = Environment::GetCurrent(context);

#define V(lcname, ucname) env->SetMethod(target, #lcname, ucname);
  VALUE_METHOD_MAP(V)
#undef V

  env->SetMethod(target, "isAnyArrayBuffer", IsAnyArrayBuffer);

#define V(name, _)                                                            \
  target->Set(context,                                                        \
              FIXED_ONE_BYTE_STRING(env->isolate(), #name),                   \
              Integer::NewFromUnsigned(env->isolate(), index++)).FromJust();
  {
    uint32_t index = 0;
    PER_ISOLATE_PRIVATE_SYMBOL_PROPERTIES(V)
  }
#undef V

  target->DefineOwnProperty(
    env->context(),
    OneByteString(env->isolate(), "pushValToArrayMax"),
    Integer::NewFromUnsigned(env->isolate(), NODE_PUSH_VAL_TO_ARRAY_MAX),
    v8::ReadOnly).FromJust();

#define V(name)                                                               \
  target->Set(context,                                                        \
              FIXED_ONE_BYTE_STRING(env->isolate(), #name),                   \
              Integer::New(env->isolate(), Promise::PromiseState::name))      \
    .FromJust()
  V(kPending);
  V(kFulfilled);
  V(kRejected);
#undef V

  env->SetMethod(target, "getHiddenValue", GetHiddenValue);
  env->SetMethod(target, "setHiddenValue", SetHiddenValue);
  env->SetMethod(target, "getPromiseDetails", GetPromiseDetails);
  env->SetMethod(target, "getProxyDetails", GetProxyDetails);
  env->SetMethod(target, "safeToString", SafeToString);

  env->SetMethod(target, "startSigintWatchdog", StartSigintWatchdog);
  env->SetMethod(target, "stopSigintWatchdog", StopSigintWatchdog);
  env->SetMethod(target, "watchdogHasPendingSigint", WatchdogHasPendingSigint);

  env->SetMethod(target, "createPromise", CreatePromise);
  env->SetMethod(target, "promiseResolve", PromiseResolve);
  env->SetMethod(target, "promiseReject", PromiseReject);
  // start custom setter here - kp96
  env->SetMethod(target, "getFunctionArgTypes", GetFunctionArgTypes);
  // end custom setter here
}

}  // namespace util
}  // namespace node

NODE_BUILTIN_MODULE_CONTEXT_AWARE(util, node::util::Initialize)
