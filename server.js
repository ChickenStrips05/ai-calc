const http = require("http")
const fakeResponse = false

const server = http.createServer(async (req, res) => {
    const url = new URL(req.url, "http://localhost")
    console.log(req.url)
    if (url.pathname == "/generate" && url.searchParams.get("q")) {
        const q = url.searchParams.get("q")

        if (!fakeResponse) {
            const response = await fetch("http://192.168.0.2:8888/v1/chat/completions", {
                method: "POST",
                body: JSON.stringify({
                    messages: [
                        {
                            role: "system", content: `You are a straightforward AI assistant. Respond to the user's requests in the most simple manner possible using the fewest words.
Be breif and consise. Do not use any formatting and keep answers short.`},
                        { role: "user", content: q }
                    ],
                    stream: false
                }),
                headers: { "content-type": "application/json" }
            })

            const json = await response.json()
            res.writeHead(200, { "content-type": "text/plain" })
            console.log(json.choices[0].message)
            const final = json.choices[0].message.content.replaceAll("\n", " ").replaceAll("$", "")
            res.end(final)
        } else {
            res.writeHead(200, { "content-type": "text/plain" })
            res.end(`${q}? uh i dont know`)
        }

    } else {
        res.writeHead(400, { "content-type": "text/plain" })
        res.end()
    }
})

server.listen(80)
