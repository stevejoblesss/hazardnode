"use client";

import { useEffect, useState } from "react";

export default function Home() {

  const [nodes, setNodes] = useState<any[]>([]);

  async function loadData() {
    const res = await fetch("/api/node");
    const data = await res.json();
    setNodes(data.nodes);
  }

  useEffect(() => {
    loadData();
    const interval = setInterval(loadData, 2000);
    return () => clearInterval(interval);
  }, []);

  return (
    <main style={{ padding: 40 }}>
      <h1>Hazard Node Dashboard</h1>

      {nodes.map((n, i) => (
        <div key={i} style={{ border: "1px solid gray", margin: 10, padding: 10 }}>
          <p>ID: {n.id}</p>
          <p>Temp: {n.temp}</p>
          <p>Humidity: {n.hum}</p>
          <p>Pitch: {n.pitch}</p>
          <p>Roll: {n.roll}</p>
          <p>Status: {n.danger ? "DANGER" : "Safe"}</p>
        </div>
      ))}
    </main>
  );
}
