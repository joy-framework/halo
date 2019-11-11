(import build/halo :as halo)

(defn handler [request]
  {:status 200 :body "halo" :headers {"Content-Type" "text/plain"}})

(halo/server handler 8080)
