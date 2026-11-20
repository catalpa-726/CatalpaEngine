using System;
using System.Collections.Generic;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.IO;
using System.Globalization;

namespace Catalpa.Hosting
{
    // 冷路径通过默认域管理程序集版本。每帧直接调用目标域中的委托。
    [UnmanagedFunctionPointer(CallingConvention.StdCall, CharSet = CharSet.Unicode)]
    internal delegate int Dispatch(int op, double time, string channel, string data,
        out int schema, [MarshalAs(UnmanagedType.BStr)] out string result);

    public sealed class ScriptDomain : MarshalByRefObject
    {
        private Type behaviourType;
        private readonly Dictionary<int, Dispatch> callbacks = new Dictionary<int, Dispatch>();

        public override object InitializeLifetimeService() { return null; }

        private static Type FindBehaviour(Assembly script, Type baseType)
        {
            Type candidate = null;
            foreach (var type in script.GetExportedTypes())
            {
                if (!type.IsSubclassOf(baseType) || type.IsAbstract || type.ContainsGenericParameters) continue;
                if (candidate != null) throw new ArgumentException("Script requires exactly one public concrete Behaviour.");
                candidate = type;
            }
            if (candidate == null || candidate.GetConstructor(Type.EmptyTypes) == null)
                throw new ArgumentException("Script requires one public concrete Behaviour with a public parameterless constructor.");
            return candidate;
        }

        public void Load(byte[] assembly)
        {
            behaviourType = FindBehaviour(Assembly.Load(assembly), typeof(Behaviour));
        }

        public void Validate(byte[] assembly)
        {
            var api = Assembly.ReflectionOnlyLoadFrom(typeof(Behaviour).Assembly.Location);
            AppDomain.CurrentDomain.ReflectionOnlyAssemblyResolve += delegate(object sender, ResolveEventArgs args)
            {
                return AssemblyName.ReferenceMatchesDefinition(new AssemblyName(args.Name), api.GetName())
                    ? api : Assembly.ReflectionOnlyLoad(args.Name);
            };
            FindBehaviour(Assembly.ReflectionOnlyLoad(assembly), api.GetType(typeof(Behaviour).FullName, true));
        }

        public IntPtr Create(int id, string entity, string properties)
        {
            try
            {
                var instance = (Behaviour)Activator.CreateInstance(behaviourType);
                instance.EntityId = Guid.Parse(entity);
                instance.Properties = Values.Read(properties);
                Dispatch call = delegate(int op, double dt, string channel, string data, out int schema, out string result)
                {
                    schema = 0; result = null;
                    try
                    {
                        switch (op)
                        {
                            case 0: instance.OnStart(); break;
                            case 1: instance.OnUpdate(dt); break;
                            case 2: instance.OnStop(); break;
                            case 3: instance.OnReload(); break;
                            case 4: instance.OnEvent(channel, Values.Read(data)); break;
                            case 5:
                                schema = instance.StateSchema;
                                if (schema < 1 || schema > 65535) throw new ArgumentException("Invalid state schema.");
                                result = Values.Write(instance.State);
                                break;
                            case 6: instance.RestoreState((int)dt, Values.Read(data)); break;
                            default: throw new ArgumentException("Unknown script callback.");
                        }
                        return 1;
                    }
                    catch (Exception ex) { result = ex.ToString(); return 0; }
                };
                var address = Marshal.GetFunctionPointerForDelegate(call);
                callbacks.Add(id, call);
                return address;
            }
            catch (Exception ex) { throw new InvalidOperationException(ex.GetBaseException().Message); }
        }

        public void Remove(int id) { callbacks.Remove(id); }
    }

    public static class ScriptHost
    {
        [UnmanagedFunctionPointer(CallingConvention.StdCall, CharSet = CharSet.Unicode)]
        private delegate int CreateCallback(IntPtr assembly, int count, string entity, string properties,
            out int handle, out IntPtr callback, [MarshalAs(UnmanagedType.BStr)] out string error);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate void ReleaseCallback(int handle);

        private static readonly CreateCallback create = Create;
        private static readonly ReleaseCallback release = Release;

        // CLR 宿主按非托管 ABI 传入两个函数指针槽。
        public static int Bind(string address)
        {
            // 默认域的应用根是游戏目录。跨域代理按程序集名反序列化时复用已加载的行为 API。
            var host = typeof(ScriptHost).Assembly;
            AppDomain.CurrentDomain.AssemblyResolve += delegate(object sender, ResolveEventArgs args)
            {
                return AssemblyName.ReferenceMatchesDefinition(new AssemblyName(args.Name), host.GetName()) ? host : null;
            };
            var destination = new IntPtr(long.Parse(address, CultureInfo.InvariantCulture));
            Marshal.WriteIntPtr(destination, Marshal.GetFunctionPointerForDelegate(create));
            Marshal.WriteIntPtr(destination, IntPtr.Size, Marshal.GetFunctionPointerForDelegate(release));
            return 0;
        }

        private sealed class Version
        {
            public AppDomain Domain;
            public ScriptDomain Scripts;
            public string Key;
            public int Instances;
        }
        private static readonly Dictionary<string, Version> versions = new Dictionary<string, Version>();
        private static readonly Dictionary<int, Version> instances = new Dictionary<int, Version>();
        private static int next;

        private static AppDomain NewDomain(string name)
        {
            return AppDomain.CreateDomain(name, null, new AppDomainSetup {
                ApplicationBase = Path.GetDirectoryName(typeof(ScriptHost).Assembly.Location) });
        }

        private static ScriptDomain GetScripts(AppDomain domain)
        {
            return (ScriptDomain)domain.CreateInstanceFromAndUnwrap(
                typeof(ScriptHost).Assembly.Location, typeof(ScriptDomain).FullName);
        }

        private static int Create(IntPtr assembly, int count, string entity, string properties,
            out int handle, out IntPtr callback, out string error)
        {
            handle = 0; callback = IntPtr.Zero; error = null;
            // 构建请求没有实体，只读取元数据。独立域在返回前卸载，避免阻塞运行中的场景。
            if (entity == null)
            {
                try
                {
                    var image = new byte[count];
                    Marshal.Copy(assembly, image, 0, count);
                    var domain = NewDomain("Catalpa script validation");
                    try { GetScripts(domain).Validate(image); }
                    finally { AppDomain.Unload(domain); }
                    return 1;
                }
                catch (Exception ex) { error = ex.GetBaseException().Message; return 0; }
            }
            lock (versions)
            {
                Version version = null;
                try
                {
                    var image = new byte[count];
                    Marshal.Copy(assembly, image, 0, count);
                    string key;
                    using (var hash = SHA256.Create()) key = Convert.ToBase64String(hash.ComputeHash(image));
                    if (!versions.TryGetValue(key, out version))
                    {
                        version = new Version { Key = key, Domain = NewDomain("Catalpa script " + key) };
                        version.Scripts = GetScripts(version.Domain);
                        version.Scripts.Load(image);
                        versions.Add(key, version);
                    }
                    int id = checked(++next);
                    callback = version.Scripts.Create(id, entity, properties);
                    instances.Add(id, version);
                    version.Instances++;
                    handle = id;
                    return 1;
                }
                catch (Exception ex)
                {
                    error = ex.ToString();
                    if (version != null && version.Instances == 0) Unload(version);
                    return 0;
                }
            }
        }
        private static void Unload(Version version)
        {
            versions.Remove(version.Key);
            try { AppDomain.Unload(version.Domain); } catch (CannotUnloadAppDomainException) {}
        }
        private static void Release(int handle)
        {
            lock (versions)
            {
                Version version;
                if (!instances.TryGetValue(handle, out version)) return;
                instances.Remove(handle);
                if (--version.Instances == 0) Unload(version);
                else version.Scripts.Remove(handle);
            }
        }
    }
}
