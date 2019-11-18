(import build/halo :as halo)

(defn handler [request]
  (print (string/format "%q" request))
  {:status 200 :body "plain text body" :headers {"Content-Type" "text/plain"}})

(halo/server handler 8080)
