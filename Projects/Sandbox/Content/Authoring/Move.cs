using System;
using Catalpa;

public sealed class Move : Behaviour
{
    // 速度单位为米/秒，deltaTime 单位为秒。
    public override void OnUpdate(double deltaTime)
    {
        object value;
        double speed = Properties.TryGetValue("speed", out value) ? Convert.ToDouble(value) : 0.5;
        Translate(speed * deltaTime, 0, 0);
    }
}
