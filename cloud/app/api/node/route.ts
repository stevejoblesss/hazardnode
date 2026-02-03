let nodeStore: any[] = [];

export async function POST(req: Request) {

    const data = await req.json();

    const existingIndex = nodeStore.findIndex(n => n.id === data.id);

    if (existingIndex >= 0) {
        nodeStore[existingIndex] = data;
    } else {
        nodeStore.push(data);
    }

    console.log("Received node:", data);

    return Response.json({ success: true });
}

export async function GET() {
    return Response.json({ nodes: nodeStore });
}
