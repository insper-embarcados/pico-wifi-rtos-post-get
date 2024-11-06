from flask import Flask, request, render_template_string

app = Flask(__name__)

# Variable to hold received data
received_data = ""

# Route to handle the index page
@app.route("/")
def index():
    global received_data
    return render_template_string("""
        <html>
        <body>
            <h1>Received Data</h1>
            <p>{{ data }}</p>
        </body>
        </html>
    """, data=received_data)

# Route to handle the POST request
@app.route("/post_data", methods=["POST"])
def post_data():
    global received_data
    received_data = request.form.get("dado", "No data received")  # Access "dado" from the form data
    return "Data received", 200


# Route to handle the GET request
@app.route("/get_data", methods=["GET"])
def get_data():
    global received_data
    received_data = request.args.get("dado", "No data received")  # Access "dado" from the query string
    return "22", 200  # Return the value 22 as the response content

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=True)
