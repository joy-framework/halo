## halo

Halo is an http server for the [janet programming language](https://github.com/janet-lang/janet)

```clojure
(import halo)

(defn handler [request]
  {:status 200 :body "welcome to halo" :headers {"Content-Type" "text/plain"}})

(halo/server handler 9001)
```
