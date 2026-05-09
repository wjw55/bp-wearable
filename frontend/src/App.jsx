import { useState, useEffect, useRef } from 'react';
import {
    LineChart, Line, XAxis, YAxis, Tooltip,
    ResponsiveContainer, ReferenceLine
} from 'recharts';

const WS_URL = 'ws://localhost:3001';

function BpmGauge({ value, label }) {
    const radius = 54;
    const circumference = 2 * Math.PI * radius;
    const max = 200;
    const min = 40;
    const pct = Math.min(Math.max((value - min) / (max - min), 0), 1);
    const dash = pct * circumference;

    const color = value < 60 ? '#38bdf8' : value < 100 ? '#22d3ee' : value < 140 ? '#facc15' : '#f87171';

    return (
        <div style={{ textAlign: 'center' }}>
            <svg width="140" height="140" viewBox="0 0 140 140">
                <circle cx="70" cy="70" r={radius} fill="none" stroke="#1e293b" strokeWidth="10" />
                <circle
                    cx="70" cy="70" r={radius} fill="none"
                    stroke={color} strokeWidth="10"
                    strokeDasharray={`${dash} ${circumference}`}
                    strokeLinecap="round"
                    transform="rotate(-90 70 70)"
                    style={{ transition: 'stroke-dasharray 0.6s ease, stroke 0.4s ease' }}
                />
                <text x="70" y="65" textAnchor="middle" fill="white" fontSize="28" fontFamily="'DM Mono', monospace" fontWeight="700">
                    {value > 0 ? value : '--'}
                </text>
                <text x="70" y="85" textAnchor="middle" fill="#64748b" fontSize="11" fontFamily="sans-serif">
                    {label}
                </text>
            </svg>
        </div>
    );
}

function StatusPill({ connected, fingerDetected }) {
    return (
        <div style={{ display: 'flex', gap: '10px', justifyContent: 'center', flexWrap: 'wrap' }}>
            <span style={{
                padding: '4px 12px', borderRadius: '999px', fontSize: '12px', fontFamily: 'monospace',
                background: connected ? '#052e16' : '#1c0a0a',
                color: connected ? '#4ade80' : '#f87171',
                border: `1px solid ${connected ? '#166534' : '#7f1d1d'}`
            }}>
                {connected ? '● LIVE' : '○ DISCONNECTED'}
            </span>
            <span style={{
                padding: '4px 12px', borderRadius: '999px', fontSize: '12px', fontFamily: 'monospace',
                background: fingerDetected ? '#052e16' : '#1c1917',
                color: fingerDetected ? '#4ade80' : '#78716c',
                border: `1px solid ${fingerDetected ? '#166534' : '#44403c'}`
            }}>
                {fingerDetected ? '● FINGER DETECTED' : '○ NO FINGER'}
            </span>
        </div>
    );
}

const CustomTooltip = ({ active, payload, label }) => {
    if (!active || !payload?.length) return null;
    return (
        <div style={{
            background: '#0f172a', border: '1px solid #334155',
            borderRadius: '8px', padding: '8px 12px', fontSize: '13px', fontFamily: 'monospace'
        }}>
            <div style={{ color: '#94a3b8' }}>{new Date(label).toLocaleTimeString()}</div>
            <div style={{ color: '#22d3ee' }}>{payload[0].value} BPM</div>
        </div>
    );
};

const BPTooltip = ({ active, payload, label }) => {
    if (!active || !payload?.length) return null;
    return (
        <div style={{
            background: '#0f172a', border: '1px solid #334155',
            borderRadius: '8px', padding: '8px 12px', fontSize: '13px', fontFamily: 'monospace'
        }}>
            <div style={{ color: '#94a3b8' }}>{new Date(label).toLocaleTimeString()}</div>
            {payload.map(p => (
                <div key={p.dataKey} style={{ color: p.stroke }}>
                    {p.dataKey.charAt(0).toUpperCase() + p.dataKey.slice(1)}: {p.value} mmHg
                </div>
            ))}
        </div>
    );
};

function getWarnings(bpm, systolic, diastolic) {
    const warnings = [];

    if (bpm > 0) {
        if (bpm < 60) warnings.push({ type: 'info', msg: 'Low heart rate (Bradycardia) — BPM below 60' });
        if (bpm >= 100 && bpm < 140) warnings.push({ type: 'warn', msg: 'Elevated heart rate (Tachycardia) — BPM above 100' });
        if (bpm >= 140) warnings.push({ type: 'danger', msg: 'High heart rate — BPM above 140, seek medical attention' });
    }

    if (systolic > 0) {
        if (systolic < 90) warnings.push({ type: 'info', msg: 'Low blood pressure (Hypotension) — Systolic below 90 mmHg' });
        if (systolic >= 130 && systolic < 180) warnings.push({ type: 'warn', msg: 'High blood pressure (Stage 1–2) — Systolic above 130 mmHg' });
        if (systolic >= 180) warnings.push({ type: 'danger', msg: 'Hypertensive Crisis — Systolic above 180 mmHg, seek emergency care' });
    }

    if (diastolic > 0) {
        if (diastolic < 60) warnings.push({ type: 'info', msg: 'Low diastolic pressure — below 60 mmHg' });
        if (diastolic >= 80 && diastolic < 120) warnings.push({ type: 'warn', msg: 'Elevated diastolic pressure — above 80 mmHg' });
        if (diastolic >= 120) warnings.push({ type: 'danger', msg: 'Hypertensive Crisis — Diastolic above 120 mmHg, seek emergency care' });
    }

    return warnings;
}

export default function App() {
    const [reading, setReading] = useState(null);
    const [history, setHistory] = useState([]);
    const [connected, setConnected] = useState(false);
    const wsRef = useRef(null);
    const { systolic, diastolic } = reading || {};

    useEffect(() => {
        function connect() {
            const ws = new WebSocket(WS_URL);
            wsRef.current = ws;

            ws.onopen = () => setConnected(true);
            ws.onclose = () => {
                setConnected(false);
                setTimeout(connect, 3000); // auto-reconnect
            };
            ws.onmessage = (e) => {
                const { reading, history } = JSON.parse(e.data);
                setReading(reading);
                setHistory(history.map(h => ({ ...h, time: h.time })));
            };
        }
        connect();
        return () => wsRef.current?.close();
    }, []);

    const bpm = reading?.avgBpm ?? 0;
    const ir = reading?.ir ?? 0;
    const finger = reading?.fingerDetected ?? false;

    const bpmZone =
        bpm === 0 ? { label: '—', color: '#475569' } :
            bpm < 60 ? { label: 'BRADYCARDIA', color: '#38bdf8' } :
                bpm < 100 ? { label: 'NORMAL', color: '#22d3ee' } :
                    bpm < 140 ? { label: 'ELEVATED', color: '#facc15' } :
                        { label: 'HIGH', color: '#f87171' };

    return (
        <div style={{
            minHeight: '100vh', background: '#020917',
            fontFamily: "'DM Mono', 'Courier New', monospace",
            color: 'white', padding: '24px',
            backgroundImage: 'radial-gradient(ellipse at 20% 20%, #0c1a3a 0%, transparent 60%), radial-gradient(ellipse at 80% 80%, #0a1628 0%, transparent 60%)'
        }}>
            {/* Google Font */}
            <style>{`@import url('https://fonts.googleapis.com/css2?family=DM+Mono:wght@400;500&display=swap');
                    *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }
                    html, body, #root { height: 100%; margin: 0; padding: 0; }
            `}</style>

            {/* Header */}
            <div style={{ maxWidth: '900px', margin: '0 auto' }}>
                <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start', marginBottom: '8px' }}>
                    <div>
                        <h1 style={{ margin: 0, fontSize: '13px', letterSpacing: '0.2em', color: '#475569', textTransform: 'uppercase' }}>
                            MAX30102 · ESP32
                        </h1>
                        <h2 style={{ margin: '4px 0 0', fontSize: '22px', fontWeight: '500', letterSpacing: '0.05em' }}>
                            Heart Rate & BP Monitor
                        </h2>
                    </div>
                    <div style={{ fontSize: '11px', color: '#334155', textAlign: 'right' }}>
                        {new Date().toLocaleDateString()}<br />
                        <span id="clock" style={{ color: '#475569' }}>{new Date().toLocaleTimeString()}</span>
                    </div>
                </div>

                <StatusPill connected={connected} fingerDetected={finger} />

                {/* Patient Profile */}
                <div style={{
                    background: '#0b1628', border: '1px solid #1e3a5f',
                    borderRadius: '16px', padding: '16px 24px', marginTop: '16px',
                    display: 'flex', alignItems: 'center', gap: '20px', flexWrap: 'wrap'
                }}>
                    <div style={{
                        width: '48px', height: '48px', borderRadius: '50%',
                        background: '#1e3a5f', display: 'flex', alignItems: 'center',
                        justifyContent: 'center', fontSize: '20px', flexShrink: 0
                    }}>
                        👤
                    </div>
                    <div style={{ flex: 1, minWidth: '180px' }}>
                        <div style={{ fontSize: '15px', fontWeight: '500', color: '#e2e8f0' }}>John</div>
                        <div style={{ fontSize: '11px', color: '#475569', marginTop: '2px', letterSpacing: '0.1em' }}>
                            ID: PT-00421 &nbsp;|&nbsp; DOB: 1985-03-12 &nbsp;|&nbsp; M
                        </div>
                    </div>
                    <div style={{ display: 'flex', gap: '24px', flexWrap: 'wrap' }}>
                        {[
                            { label: 'AGE', value: '40' },
                            { label: 'BLOOD TYPE', value: 'O+' },
                            { label: 'WEIGHT', value: '72 kg' },
                            { label: 'HEIGHT', value: '175 cm' },
                        ].map(({ label, value }) => (
                            <div key={label} style={{ textAlign: 'center' }}>
                                <div style={{ fontSize: '10px', color: '#475569', letterSpacing: '0.12em' }}>{label}</div>
                                <div style={{ fontSize: '16px', fontWeight: '500', color: '#94a3b8', marginTop: '2px' }}>{value}</div>
                            </div>
                        ))}
                    </div>
                </div>

                {/* Warnings */}
                {(() => {
                    const warnings = getWarnings(bpm, systolic, diastolic);
                    if (!warnings.length || !finger) return null;

                    const styles = {
                        info: { bg: '#0c1e3e', border: '#1e3a5f', color: '#38bdf8', icon: 'ℹ️' },
                        warn: { bg: '#1c1a00', border: '#854d0e', color: '#facc15', icon: '⚠️' },
                        danger: { bg: '#1c0a0a', border: '#7f1d1d', color: '#f87171', icon: '🚨' },
                    };

                    return (
                        <div style={{ display: 'flex', flexDirection: 'column', gap: '8px', marginTop: '16px' }}>
                            {warnings.map((w, i) => {
                                const s = styles[w.type];
                                return (
                                    <div key={i} style={{
                                        background: s.bg, border: `1px solid ${s.border}`,
                                        borderRadius: '12px', padding: '12px 16px',
                                        display: 'flex', alignItems: 'center', gap: '10px'
                                    }}>
                                        <span style={{ fontSize: '16px' }}>{s.icon}</span>
                                        <span style={{ fontSize: '12px', color: s.color, letterSpacing: '0.05em' }}>
                                            {w.msg}
                                        </span>
                                    </div>
                                );
                            })}
                        </div>
                    );
                })()}

                {/* Main cards */}
                <div style={{
                    display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(200px, 1fr))',
                    gap: '16px', marginTop: '24px'
                }}>
                    {/* BPM Card */}
                    <div style={{
                        background: '#0b1628', border: '1px solid #1e3a5f',
                        borderRadius: '16px', padding: '24px', textAlign: 'center'
                    }}>
                        <div style={{ fontSize: '11px', color: '#475569', letterSpacing: '0.15em', marginBottom: '12px' }}>
                            AVG HEART RATE
                        </div>
                        <BpmGauge value={bpm} label="BPM" />
                        <div style={{
                            marginTop: '8px', fontSize: '11px', letterSpacing: '0.12em',
                            color: bpmZone.color, fontWeight: '500'
                        }}>
                            {bpmZone.label}
                        </div>
                    </div>

                    {/* Stats column */}
                    <div style={{ display: 'flex', flexDirection: 'column', gap: '16px' }}>
                        {/* Blood Pressure */}
                        <div style={{
                            background: '#0b1628', border: '1px solid #1e3a5f',
                            borderRadius: '16px', padding: '20px', flex: 1
                        }}>
                            <div style={{ fontSize: '11px', color: '#475569', letterSpacing: '0.15em' }}>ESTIMATED BLOOD PRESSURE</div>
                            <div style={{ fontSize: '42px', fontWeight: '700', marginTop: '4px', color: '#e2e8f0' }}>
                                {systolic > 0 ? `${systolic}/${diastolic}` : '--'}
                            </div>
                            <div style={{ fontSize: '11px', color: '#475569', marginTop: '4px' }}>mmHg</div>
                        </div>
                        {/* IR Value */}
                        <div style={{
                            background: '#0b1628', border: '1px solid #1e3a5f',
                            borderRadius: '16px', padding: '20px', flex: 1
                        }}>
                            <div style={{ fontSize: '11px', color: '#475569', letterSpacing: '0.15em' }}>IR SENSOR</div>
                            <div style={{ fontSize: '28px', fontWeight: '500', marginTop: '4px', color: '#94a3b8' }}>
                                {ir.toLocaleString()}
                            </div>
                            <div style={{ marginTop: '8px', height: '4px', background: '#1e293b', borderRadius: '2px' }}>
                                <div style={{
                                    height: '100%', borderRadius: '2px', background: '#22d3ee',
                                    width: `${Math.min((ir / 100000) * 100, 100)}%`,
                                    transition: 'width 0.5s ease'
                                }} />
                            </div>
                        </div>
                    </div>
                </div>

                {/* History Charts */}
                <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '16px', marginTop: '16px' }}>

                    {/* BPM History */}
                    <div style={{ background: '#0b1628', border: '1px solid #1e3a5f', borderRadius: '16px', padding: '24px' }}>
                        <div style={{ fontSize: '11px', color: '#475569', letterSpacing: '0.15em', marginBottom: '16px' }}>
                            BPM HISTORY (LAST {history.length} READINGS)
                        </div>
                        {history.length > 1 ? (
                            <ResponsiveContainer width="100%" height={160}>
                                <LineChart data={history}>
                                    <XAxis
                                        dataKey="time"
                                        tickFormatter={t => new Date(t).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' })}
                                        tick={{ fill: '#334155', fontSize: 10 }}
                                        axisLine={false} tickLine={false}
                                    />
                                    <YAxis domain={['auto', 'auto']} tick={{ fill: '#334155', fontSize: 10 }} axisLine={false} tickLine={false} />
                                    <Tooltip content={<CustomTooltip />} />
                                    <ReferenceLine y={100} stroke="#facc1530" strokeDasharray="4 4" />
                                    <ReferenceLine y={60} stroke="#38bdf830" strokeDasharray="4 4" />
                                    <Line type="monotone" dataKey="bpm" stroke="#22d3ee" strokeWidth={2} dot={false} activeDot={{ r: 4, fill: '#22d3ee' }} />
                                </LineChart>
                            </ResponsiveContainer>
                        ) : (
                            <div style={{ height: '160px', display: 'flex', alignItems: 'center', justifyContent: 'center', color: '#334155', fontSize: '13px' }}>
                                Waiting for data…
                            </div>
                        )}
                    </div>

                    {/* BP History */}
                    <div style={{ background: '#0b1628', border: '1px solid #1e3a5f', borderRadius: '16px', padding: '24px' }}>
                        <div style={{ fontSize: '11px', color: '#475569', letterSpacing: '0.15em', marginBottom: '16px' }}>
                            BP HISTORY (LAST {history.length} READINGS)
                        </div>
                        {history.length > 1 ? (
                            <ResponsiveContainer width="100%" height={160}>
                                <LineChart data={history}>
                                    <XAxis
                                        dataKey="time"
                                        tickFormatter={t => new Date(t).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' })}
                                        tick={{ fill: '#334155', fontSize: 10 }}
                                        axisLine={false} tickLine={false}
                                    />
                                    <YAxis domain={[40, 200]} tick={{ fill: '#334155', fontSize: 10 }} axisLine={false} tickLine={false} />
                                    <Tooltip content={<BPTooltip />} />
                                    <ReferenceLine y={130} stroke="#f8717130" strokeDasharray="4 4" />
                                    <ReferenceLine y={90} stroke="#facc1530" strokeDasharray="4 4" />
                                    <Line type="monotone" dataKey="systolic" stroke="#f87171" strokeWidth={2} dot={false} activeDot={{ r: 4, fill: '#f87171' }} />
                                    <Line type="monotone" dataKey="diastolic" stroke="#a78bfa" strokeWidth={2} dot={false} activeDot={{ r: 4, fill: '#a78bfa' }} />
                                </LineChart>
                            </ResponsiveContainer>
                        ) : (
                            <div style={{ height: '160px', display: 'flex', alignItems: 'center', justifyContent: 'center', color: '#334155', fontSize: '13px' }}>
                                Waiting for data…
                            </div>
                        )}
                        {/* Legend */}
                        <div style={{ display: 'flex', gap: '16px', marginTop: '12px' }}>
                            <div style={{ display: 'flex', alignItems: 'center', gap: '6px', fontSize: '11px', color: '#94a3b8' }}>
                                <div style={{ width: '16px', height: '2px', background: '#f87171', borderRadius: '2px' }} /> SYSTOLIC
                            </div>
                            <div style={{ display: 'flex', alignItems: 'center', gap: '6px', fontSize: '11px', color: '#94a3b8' }}>
                                <div style={{ width: '16px', height: '2px', background: '#a78bfa', borderRadius: '2px' }} /> DIASTOLIC
                            </div>
                        </div>
                    </div>

                </div>

                {/* Footer */}
                <div style={{ textAlign: 'center', marginTop: '20px', fontSize: '11px', color: '#1e3a5f', letterSpacing: '0.1em' }}>
                    SENSOR · MAX30102 &nbsp;|&nbsp; MCU · ESP32 &nbsp;|&nbsp; TRANSPORT · HTTP+WS
                </div>
            </div>
        </div>
    );
}