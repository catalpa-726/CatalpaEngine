using System;
using System.Collections.Generic;
using Catalpa;

public sealed class DynamicLifecycle : Behaviour
{
    public override void OnStart()
    {
        State["child"] = CreateEntity("Script-created entity", 0, 1, 0).ToString("D");
        State["pulses"] = 0L;
        State["elapsed"] = 0.0;
        ChecksumAsync("Catalpa async owner continuation", "checksum_ready");
    }

    public override void OnUpdate(double deltaTime)
    {
        double elapsed = Convert.ToDouble(State["elapsed"]) + deltaTime;
        double ticks = Math.Floor(elapsed);
        State["elapsed"] = elapsed - ticks;
        if (ticks > 0)
            Translate(0, 0.05 * ticks, 0);
    }

    public override void OnEvent(string channel, IDictionary<string, object> values)
    {
        object kind;
        if (channel == "collision" || (channel == "ui" && values.TryGetValue("type", out kind) && Equals(kind, "click")))
            State["pulses"] = Convert.ToInt64(State["pulses"]) + 1;
        if (channel == "@continuation/checksum_ready" && ((string)values["checksum"]).Length != 64)
            throw new InvalidOperationException("Invalid checksum");
    }
}
