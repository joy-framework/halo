(def- status-codes {100 "Continue"
                    101 "Switching Protocols"
                    200 "OK"
                    201 "Created"
                    202 "Accepted"
                    203 "Non-Authoritative Information"
                    204 "No Content"
                    205 "Reset Content"
                    206 "Partial Content"
                    300 "Multiple Choices"
                    301 "Moved Permanently"
                    302 "Found"
                    303 "See Other"
                    304 "Not Modified"
                    305 "Use Proxy"
                    306 "(Unused)"
                    307 "Temporary Redirect"
                    400 "Bad Request"
                    401 "Unauthorized"
                    402 "Payment Required"
                    403 "Forbidden"
                    404 "Not Found"
                    405 "Method Not Allowed"
                    406 "Not Acceptable"
                    407 "Proxy Authentication Required"
                    408 "Request Timeout"
                    409 "Conflict"
                    410 "Gone"
                    411 "Length Required"
                    412 "Precondition Failed"
                    413 "Request Entity Too Large"
                    414 "Request-URI Too Long"
                    415 "Unsupported Media Type"
                    416 "Requested Range Not Satisfiable"
                    417 "Expectation Failed"
                    500 "Internal Server Error"
                    501 "Not Implemented"
                    502 "Bad Gateway"
                    503 "Service Unavailable"
                    504 "Gateway Timeout"
                    505 "HTTP Version Not Supported"})

(def- mime-types {"jpg"  "image/jpeg"
                  "jpeg" "image/jpeg"
                  "png"  "image/png"
                  "gif"  "image/gif"
                  "ico"  "image/x-icon"
                  "html" "text/html"
                  "css"  "text/css"
                  "svg"  "text/svg"
                  "txt"  "text/plain"
                  "js"   "application/javascript"
                  "json" "application/json"})

(def- headers-peg '{:s (set "\r\n")
                    :S (if-not :s 1)
                    :key (choice (range "AZ" "az" "09") (set "-_"))
                    :value (choice (range "AZ" "az" "09") :S)
                    :main (sequence (<- (some :key)) ": " (<- (some :value)) "\r\n")})

(def- request-line-peg '{:method (some (range "AZ"))
                         :crlf (some (choice (sequence "\r\n") (sequence "%0d%0a")))
                         :uri (some (choice (range "AZ" "az" "09") (set "-_/?#%=&@():*.")))
                         :number (range "09")
                         :version (sequence "HTTP/" :number "." :number)
                         :main (sequence (<- :method) " " (<- :uri) " " (<- :version) "\r\n")})


(def- body-peg '{:body (any (choice (range "az" "AZ" "09") (set " \":-_.@!#$%^&*()=~+{}[]|\\/>`<`?',\r\n\t\0")))
                 :main (sequence "\r\n\r\n" (<- :body))})


(defn- capture [patt]
  (peg/compile ~(any (+ (* ,patt) 1))))

(def- headers (partial peg/match (capture headers-peg)))
(def- request-line (partial peg/match (capture request-line-peg)))
(def- body (partial peg/match (capture body-peg)))


(defn- ext [s]
  (last (string/split "." s)))


(defn- close? [{:body body :headers headers}]
  (let [content-length (scan-number (get headers "Content-Length" "0"))
        body-length (length body)]
    (= content-length body)))


(defn- response-headers [headers-struct]
  (as-> (pairs headers-struct) ?
        (map (fn [[k v]] (string/format "%s: %s" k v)) ?)
        (string/join ? "\r\n")))


(defn- http-response [response-struct]
  (let [{:status status :body body :headers headers :file file} response-struct
        body (or body "")
        body (if (and file (os/stat file))
              (try
                (slurp file)
                ([_] ""))
              body)
        ext (when file (ext file))
        status (or status (if (empty? body)
                            404
                            200))
        headers (merge {"Content-Length" (string (length body))
                        "Content-Type" (get mime-types ext "text/plain")}
                       (or headers {}))
        status-code (get status-codes status)
        response-headers (response-headers headers)
        response-string (string/format "HTTP/1.1 %d %s\r\n%s\r\n\r\n" status status-code response-headers)]
    (if (empty? body)
      response-string
      (string response-string body))))


(defn- http-request [buf]
  (let [request-string (string buf)
        [method uri version] (request-line request-string)
        headers (apply struct (headers request-string))
        body (first (body request-string))]
    {:body body :headers headers :method method :uri uri :version version}))


(defn- connection-handler
  "A connection handler"
  [handler]
  (fn [stream]
    (defer (:close stream)
      (def b @"")
      (while (:read stream 1024 b)
        (->> (http-request b)
             (handler)
             (http-response)
             (:write stream))
        (buffer/clear b)))))


(defn server
  `
  Listens for http requests on the given port

  Example:

  (import halo)

  (def handler [request]
    {:status 200 :body "testing halo" :headers {"content-type" "text/plain"}})

  (halo/server handler 9001)`
  [handler port &opt ip-address]
  (default ip-address "localhost")
  (printf "Server listening on [%s:%s] ..." ip-address (string port))
  (net/server ip-address (scan-number (string port)) (connection-handler handler)))
