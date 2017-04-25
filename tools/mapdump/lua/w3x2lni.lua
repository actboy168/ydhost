local w3i = require 'w3i'
return {
    read_w3i = function (_, s)
        return w3i(s)
    end
}
