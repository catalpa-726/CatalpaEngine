using System;
using System.Collections.Generic;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Text;
using System.Web.Script.Serialization;

namespace Catalpa
{
    /// <summary>局部坐标中的三维向量，位置单位为米。</summary>
    public struct Vector3
    {
        public double X, Y, Z;
        public Vector3(double x, double y, double z) { X = x; Y = y; Z = z; }
    }

    /// <summary>每个挂载实体拥有独立实例。场景操作在当前生命周期回调成功返回后提交。</summary>
    public abstract class Behaviour
    {
        public Guid EntityId { get; internal set; }
        public IDictionary<string, object> Properties { get; internal set; }
        public IDictionary<string, object> State { get; private set; }
        public virtual int StateSchema { get { return 1; } }

        protected Behaviour()
        {
            Properties = new Dictionary<string, object>();
            State = new Dictionary<string, object>();
        }

        public virtual void OnStart() {}
        public virtual void OnUpdate(double deltaTime) {}
        public virtual void OnStop() {}
        public virtual void OnReload() {}
        public virtual void OnEvent(string channel, IDictionary<string, object> values) {}

        public virtual void RestoreState(int schema, IDictionary<string, object> previous)
        {
            if (schema != StateSchema)
                throw new InvalidOperationException("Override RestoreState to migrate a different state schema.");
            State = previous;
        }

        public Vector3 Position
        {
            get
            {
                double x = 0, y = 0, z = 0;
                Check(Native.CatalpaScriptPosition(0, ref x, ref y, ref z));
                return new Vector3(x, y, z);
            }
            set { Check(Native.CatalpaScriptPosition(1, ref value.X, ref value.Y, ref value.Z)); }
        }
        public void Translate(double x, double y, double z)
        {
            Check(Native.CatalpaScriptPosition(2, ref x, ref y, ref z));
        }
        public Guid CreateEntity(string name, double x, double y, double z)
        {
            var id = new byte[37];
            var bytes = Utf8(name);
            Check(Native.CatalpaScriptCreate(bytes, bytes.Length, x, y, z, id));
            return Guid.Parse(Encoding.ASCII.GetString(id, 0, 36));
        }
        public void DestroyEntity(Guid entity) { Action(0, entity.ToString("D"), ""); }
        public void Emit(string channel, string value) { Action(1, channel, value); }
        public long ChecksumAsync(string text, string callback) { return Action(2, text, callback); }
        public void CancelChecksum(long ticket) { Action(3, ticket.ToString(CultureInfo.InvariantCulture), ""); }
        public void LoadScene(Guid scene) { Action(4, scene.ToString("D"), ""); }

        private static long Action(int op, string first, string second)
        {
            var a = Utf8(first);
            var b = Utf8(second);
            var result = Native.CatalpaScriptAction(op, a, a.Length, b, b.Length);
            Check(result > 0 ? 1 : 0);
            return result;
        }
        private static byte[] Utf8(string text)
        {
            if (text == null || text.Length > 65536 || text.IndexOf('\0') >= 0)
                throw new ArgumentException("Invalid script text.");
            return Encoding.UTF8.GetBytes(text);
        }
        private static void Check(int result)
        {
            if (result == 0)
                throw new InvalidOperationException("The scene rejected the operation or no script callback is active.");
        }
    }

    internal static class Native
    {
        [DllImport("Catalpa.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int CatalpaScriptPosition(int op, ref double x, ref double y, ref double z);
        [DllImport("Catalpa.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int CatalpaScriptCreate(byte[] name, int length, double x, double y, double z, [Out] byte[] id);
        [DllImport("Catalpa.dll", CallingConvention = CallingConvention.Cdecl)]
        internal static extern long CatalpaScriptAction(int op, byte[] a, int lengthA, byte[] b, int lengthB);
    }

    internal static class Values
    {
        private static JavaScriptSerializer Serializer()
        {
            return new JavaScriptSerializer { MaxJsonLength = 16 * 1024 * 1024, RecursionLimit = 12 };
        }

        internal static Dictionary<string, object> Read(string json)
        {
            var fields = Serializer().Deserialize<Dictionary<string, Dictionary<string, object>>>(json);
            var values = new Dictionary<string, object>(StringComparer.Ordinal);
            foreach (var field in fields)
            {
                object value = field.Value["value"];
                switch ((string)field.Value["type"])
                {
                    case "entity": value = Guid.Parse((string)value); break;
                    case "int64": value = Convert.ToInt64(value); break;
                    case "number": value = Convert.ToDouble(value, CultureInfo.InvariantCulture); break;
                    case "float3":
                        var v = (System.Collections.IList)value;
                        value = new Vector3(Convert.ToDouble(v[0]), Convert.ToDouble(v[1]), Convert.ToDouble(v[2]));
                        break;
                    case "bool": case "string": break;
                    default: throw new ArgumentException("Unknown script value.");
                }
                values.Add(field.Key, value);
            }
            return values;
        }

        internal static string Write(IDictionary<string, object> values)
        {
            var fields = new Dictionary<string, object>();
            foreach (var pair in values)
            {
                object value = pair.Value;
                string tag;
                if (value is Guid) { tag = "entity"; value = ((Guid)value).ToString("D"); }
                else if (value is Vector3)
                {
                    tag = "float3";
                    var v = (Vector3)value;
                    value = new double[] { v.X, v.Y, v.Z };
                }
                else if (value is float || value is double) tag = "number";
                else if (value is int || value is long) tag = "int64";
                else if (value is bool) tag = "bool";
                else if (value is string) tag = "string";
                else throw new ArgumentException("Unsupported state value: " + pair.Key);
                fields.Add(pair.Key, new { type = tag, value = value });
            }
            return Serializer().Serialize(fields);
        }
    }
}
