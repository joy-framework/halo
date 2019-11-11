(defn response-middleware
  "Change response keyword keys to string keys"
  [handler]
  (fn [request]
    # TODO: Figure out why janet_ckeywordv doesn't work in halo.c
    (let [res (handler request)
          {:status status :body body :headers headers} res]
     {"status" status "body" body "headers" headers})))

(defn server
  "Creates a simple http server"
  [handler port &opt ip-address]
  (default ip-address "localhost")
  (let [interface (if (= "localhost" ip-address)
                     (string port)
                     (string/format "%s:%d" ip-address port))]
    (print (string/format "Server listening on [%s:%d] ..." ip-address port))
    (start (response-middleware handler) interface)))
