import React, { useMemo } from 'react';
import { useDevice } from '../contexts/DeviceContext';

export const HeroDial: React.FC = () => {
  const { telemetry, settings, setLedTarget } = useDevice();

  const size = 320;
  const center = size / 2;
  const radiusOuter = size / 2 - 20;
  const radiusInner = radiusOuter - 30;

  // Calculate positions
  const getPoint = (angleDeg: number, r: number) => {
    const rad = (angleDeg - 90) * (Math.PI / 180);
    return {
      x: center + r * Math.cos(rad),
      y: center + r * Math.sin(rad),
    };
  };

  const currentPhase = telemetry.phaseDeg;
  const heavySpot = telemetry.heavyDeg;
  const addSpot = telemetry.addDeg;
  const removeSpot = telemetry.removeDeg || heavySpot;
  const zeroOffset = settings.model.zeroOffsetDeg;

  // Generate ticks for outer ring
  const ticks = useMemo(() => {
    const t = [];
    for (let i = 0; i < 360; i += 10) {
      const isMajor = i % 30 === 0;
      t.push({ angle: i, isMajor });
    }
    return t;
  }, []);

  const handleDialClick = (e: React.MouseEvent<SVGSVGElement>) => {
    const rect = e.currentTarget.getBoundingClientRect();
    const x = e.clientX - rect.left - center;
    const y = e.clientY - rect.top - center;
    let angle = Math.atan2(y, x) * (180 / Math.PI) + 90;
    if (angle < 0) angle += 360;
    setLedTarget(Math.round(angle));
  };

  return (
    <div className="flex flex-col items-center justify-center p-8 bg-zinc-900 rounded-3xl shadow-2xl border border-zinc-800 relative overflow-hidden">
      {/* LED Indicator */}
      <div className="absolute top-6 flex flex-col items-center gap-2 z-10">
        <div className={`w-12 h-4 rounded-full transition-all duration-150 shadow-[0_0_20px_rgba(56,189,248,0.5)] ${telemetry.ledOn ? 'bg-sky-400 shadow-sky-400/80 scale-110' : 'bg-zinc-800 shadow-none'}`} />
        <span className="text-[10px] font-mono uppercase tracking-widest text-zinc-500 font-semibold">Target LED</span>
      </div>

      {/* Dial SVG */}
      <svg width={size} height={size} className="mt-8 cursor-crosshair" onClick={handleDialClick}>
        {/* Outer Ring Background */}
        <circle cx={center} cy={center} r={radiusOuter} fill="none" stroke="#27272a" strokeWidth="2" />
        
        {/* Ticks */}
        {ticks.map((tick) => {
          const p1 = getPoint(tick.angle, radiusOuter);
          const p2 = getPoint(tick.angle, radiusOuter - (tick.isMajor ? 12 : 6));
          return (
            <g key={tick.angle}>
              <line
                x1={p1.x} y1={p1.y}
                x2={p2.x} y2={p2.y}
                stroke={tick.isMajor ? '#52525b' : '#3f3f46'}
                strokeWidth={tick.isMajor ? 2 : 1}
              />
              {tick.isMajor && (
                <text
                  x={getPoint(tick.angle, radiusOuter + 14).x}
                  y={getPoint(tick.angle, radiusOuter + 14).y}
                  fill="#71717a"
                  fontSize="10"
                  fontFamily="monospace"
                  textAnchor="middle"
                  alignmentBaseline="middle"
                  transform={`rotate(${tick.angle} ${getPoint(tick.angle, radiusOuter + 14).x} ${getPoint(tick.angle, radiusOuter + 14).y})`}
                >
                  {tick.angle}°
                </text>
              )}
            </g>
          );
        })}

        {/* Zero Offset Mark */}
        <path
          d={`M ${getPoint(zeroOffset, radiusOuter + 5).x} ${getPoint(zeroOffset, radiusOuter + 5).y} L ${getPoint(zeroOffset - 5, radiusOuter + 15).x} ${getPoint(zeroOffset - 5, radiusOuter + 15).y} L ${getPoint(zeroOffset + 5, radiusOuter + 15).x} ${getPoint(zeroOffset + 5, radiusOuter + 15).y} Z`}
          fill="#f43f5e"
        />

        {/* Inner Disc */}
        <circle cx={center} cy={center} r={radiusInner} fill="#18181b" stroke="#27272a" strokeWidth="1" />

        {/* Current Phase Indicator (Live Rotation) */}
        <g transform={`rotate(${currentPhase} ${center} ${center})`}>
          <line x1={center} y1={center} x2={center} y2={center - radiusInner + 10} stroke="#a1a1aa" strokeWidth="2" strokeDasharray="4 4" />
          <circle cx={center} cy={center - radiusInner + 10} r="4" fill="#a1a1aa" />
        </g>

        {/* Heavy Spot Pointer */}
        <g transform={`rotate(${heavySpot} ${center} ${center})`}>
          <line x1={center} y1={center} x2={center} y2={center - radiusInner + 20} stroke="#ef4444" strokeWidth="3" />
          <polygon points={`${center},${center - radiusInner + 20} ${center - 6},${center - radiusInner + 30} ${center + 6},${center - radiusInner + 30}`} fill="#ef4444" />
        </g>

        {/* Add Weight Pointer */}
        <g transform={`rotate(${addSpot} ${center} ${center})`}>
          <line x1={center} y1={center} x2={center} y2={center - radiusInner + 20} stroke="#10b981" strokeWidth="3" />
          <polygon points={`${center},${center - radiusInner + 20} ${center - 6},${center - radiusInner + 30} ${center + 6},${center - radiusInner + 30}`} fill="#10b981" />
        </g>

        {/* Center Hub */}
        <circle cx={center} cy={center} r="12" fill="#27272a" stroke="#3f3f46" strokeWidth="2" />
        <circle cx={center} cy={center} r="4" fill="#10b981" />
      </svg>

      <div className="mt-6 grid grid-cols-3 gap-8 text-center w-full">
        <div>
          <div className="text-xs text-zinc-500 uppercase tracking-wider font-semibold mb-1">Phase</div>
          <div className="text-xl font-mono text-zinc-300">{Math.round(currentPhase)}°</div>
        </div>
        <div>
          <div className="text-xs text-red-500 uppercase tracking-wider font-semibold mb-1">Heavy</div>
          <div className="text-xl font-mono text-red-400">{Math.round(heavySpot)}°</div>
        </div>
        <div>
          <div className="text-xs text-emerald-500 uppercase tracking-wider font-semibold mb-1">Add</div>
          <div className="text-xl font-mono text-emerald-400">{Math.round(addSpot)}°</div>
        </div>
      </div>
    </div>
  );
};
