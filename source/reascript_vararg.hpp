// Extracted from ReaPack's and ReaImGui's source code (LGPL v3)

/* Usage example:
static int HelloWorld(int foo, int bar) { return foo * bar; }

extern "C" REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(
  REAPER_PLUGIN_HINSTANCE instance, reaper_plugin_info_t *rec)
{
  // ...
  // API_HelloWorld and APIdef_HelloWorld must also be registered as usual
  plugin_register("APIvararg_HelloWorld",
    reinterpret_cast<void *>(&InvokeReaScriptAPI<&HelloWorld>));
  // ...
}
*/

#include <tuple>

template <typename T> struct ReaScriptAPI;

template <typename R, typename... Args> struct ReaScriptAPI<R (*)(Args...)>
{
  static const void* applyVarArg(R (*fn)(Args...), void** argv, const int argc)
  {
    if (static_cast<size_t>(argc) < sizeof...(Args))
      return nullptr;

    const auto& args{makeTuple(argv, std::index_sequence_for<Args...>{})};

    if constexpr (std::is_void_v<R>)
    {
      std::apply(fn, args);
      return nullptr;
    }
    else if constexpr (std::is_floating_point_v<R>)
    {
      const auto value{std::apply(fn, args)};
      void* storage{argv[argc - 1]};
      *static_cast<double*>(storage) = value;
      return storage;
    }
    else
    {
      // cast numbers to have the same size as a pointer to avoid warnings
      using IntPtrR = std::conditional_t<std::is_pointer_v<R>, R, uintptr_t>;
      const auto value{static_cast<IntPtrR>(std::apply(fn, args))};
      return reinterpret_cast<const void*>(value);
    }
  }

private:
  template <size_t I>
  using NthType = typename std::tuple_element<I, std::tuple<Args...>>::type;

  template <size_t... I>
  static auto makeTuple(void** argv, std::index_sequence<I...>)
  {
    // C++17 is amazing
    return std::make_tuple(
      std::is_floating_point_v<NthType<I>>
        ? *reinterpret_cast<NthType<I>*>(argv[I])
        : (NthType<I>)reinterpret_cast<uintptr_t>(argv[I])...);
  }
};

template <auto fn> const void* InvokeReaScriptAPI(void** argv, int argc)
{
  return ReaScriptAPI<decltype(fn)>::applyVarArg(fn, argv, argc);
}
