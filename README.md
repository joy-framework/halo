## halo

Halo is an http server for [janet](https://github.com/janet-lang/janet)

```clojure
(import halo)

(defn handler [request]
  {:status 200 :body "halo" :headers {"Content-Type" "text/plain"}})

(halo/server handler 8080)
```
