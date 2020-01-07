(defn server
  "Starts an http server"
  [handler port]
  (start-server handler port)
  (printf "Server listening on [localhost:%d] ..." port)
  (while true
    (poll-server)))
