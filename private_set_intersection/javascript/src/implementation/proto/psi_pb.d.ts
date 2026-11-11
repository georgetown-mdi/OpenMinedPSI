// package: psi_proto
// file: psi.proto

import * as jspb from "google-protobuf";

export class ServerSetup extends jspb.Message {
  hasRaw(): boolean;
  clearRaw(): void;
  getRaw(): ServerSetup.RawInfo | undefined;
  setRaw(value?: ServerSetup.RawInfo): void;

  hasGcs(): boolean;
  clearGcs(): void;
  getGcs(): ServerSetup.GCSInfo | undefined;
  setGcs(value?: ServerSetup.GCSInfo): void;

  hasBloomFilter(): boolean;
  clearBloomFilter(): void;
  getBloomFilter(): ServerSetup.BloomFilterInfo | undefined;
  setBloomFilter(value?: ServerSetup.BloomFilterInfo): void;

  getDataStructureCase(): ServerSetup.DataStructureCase;
  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): ServerSetup.AsObject;
  static toObject(includeInstance: boolean, msg: ServerSetup): ServerSetup.AsObject;
  static extensions: {[key: number]: jspb.ExtensionFieldInfo<jspb.Message>};
  static extensionsBinary: {[key: number]: jspb.ExtensionFieldBinaryInfo<jspb.Message>};
  static serializeBinaryToWriter(message: ServerSetup, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): ServerSetup;
  static deserializeBinaryFromReader(message: ServerSetup, reader: jspb.BinaryReader): ServerSetup;
}

export namespace ServerSetup {
  export type AsObject = {
    raw?: ServerSetup.RawInfo.AsObject,
    gcs?: ServerSetup.GCSInfo.AsObject,
    bloomFilter?: ServerSetup.BloomFilterInfo.AsObject,
  }

  export class RawInfo extends jspb.Message {
    clearEncryptedElementsList(): void;
    getEncryptedElementsList(): Array<Uint8Array | string>;
    getEncryptedElementsList_asU8(): Array<Uint8Array>;
    getEncryptedElementsList_asB64(): Array<string>;
    setEncryptedElementsList(value: Array<Uint8Array | string>): void;
    addEncryptedElements(value: Uint8Array | string, index?: number): Uint8Array | string;

    serializeBinary(): Uint8Array;
    toObject(includeInstance?: boolean): RawInfo.AsObject;
    static toObject(includeInstance: boolean, msg: RawInfo): RawInfo.AsObject;
    static extensions: {[key: number]: jspb.ExtensionFieldInfo<jspb.Message>};
    static extensionsBinary: {[key: number]: jspb.ExtensionFieldBinaryInfo<jspb.Message>};
    static serializeBinaryToWriter(message: RawInfo, writer: jspb.BinaryWriter): void;
    static deserializeBinary(bytes: Uint8Array): RawInfo;
    static deserializeBinaryFromReader(message: RawInfo, reader: jspb.BinaryReader): RawInfo;
  }

  export namespace RawInfo {
    export type AsObject = {
      encryptedElementsList: Array<Uint8Array | string>,
    }
  }

  export class GCSInfo extends jspb.Message {
    getDiv(): number;
    setDiv(value: number): void;

    getHashRange(): number;
    setHashRange(value: number): void;

    getBits(): Uint8Array | string;
    getBits_asU8(): Uint8Array;
    getBits_asB64(): string;
    setBits(value: Uint8Array | string): void;

    serializeBinary(): Uint8Array;
    toObject(includeInstance?: boolean): GCSInfo.AsObject;
    static toObject(includeInstance: boolean, msg: GCSInfo): GCSInfo.AsObject;
    static extensions: {[key: number]: jspb.ExtensionFieldInfo<jspb.Message>};
    static extensionsBinary: {[key: number]: jspb.ExtensionFieldBinaryInfo<jspb.Message>};
    static serializeBinaryToWriter(message: GCSInfo, writer: jspb.BinaryWriter): void;
    static deserializeBinary(bytes: Uint8Array): GCSInfo;
    static deserializeBinaryFromReader(message: GCSInfo, reader: jspb.BinaryReader): GCSInfo;
  }

  export namespace GCSInfo {
    export type AsObject = {
      div: number,
      hashRange: number,
      bits: Uint8Array | string,
    }
  }

  export class BloomFilterInfo extends jspb.Message {
    getNumHashFunctions(): number;
    setNumHashFunctions(value: number): void;

    getBits(): Uint8Array | string;
    getBits_asU8(): Uint8Array;
    getBits_asB64(): string;
    setBits(value: Uint8Array | string): void;

    serializeBinary(): Uint8Array;
    toObject(includeInstance?: boolean): BloomFilterInfo.AsObject;
    static toObject(includeInstance: boolean, msg: BloomFilterInfo): BloomFilterInfo.AsObject;
    static extensions: {[key: number]: jspb.ExtensionFieldInfo<jspb.Message>};
    static extensionsBinary: {[key: number]: jspb.ExtensionFieldBinaryInfo<jspb.Message>};
    static serializeBinaryToWriter(message: BloomFilterInfo, writer: jspb.BinaryWriter): void;
    static deserializeBinary(bytes: Uint8Array): BloomFilterInfo;
    static deserializeBinaryFromReader(message: BloomFilterInfo, reader: jspb.BinaryReader): BloomFilterInfo;
  }

  export namespace BloomFilterInfo {
    export type AsObject = {
      numHashFunctions: number,
      bits: Uint8Array | string,
    }
  }

  export enum DataStructureCase {
    DATA_STRUCTURE_NOT_SET = 0,
    RAW = 1,
    GCS = 2,
    BLOOM_FILTER = 3,
  }
}

export class Request extends jspb.Message {
  getRevealIntersection(): boolean;
  setRevealIntersection(value: boolean): void;

  clearEncryptedElementsList(): void;
  getEncryptedElementsList(): Array<Uint8Array | string>;
  getEncryptedElementsList_asU8(): Array<Uint8Array>;
  getEncryptedElementsList_asB64(): Array<string>;
  setEncryptedElementsList(value: Array<Uint8Array | string>): void;
  addEncryptedElements(value: Uint8Array | string, index?: number): Uint8Array | string;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): Request.AsObject;
  static toObject(includeInstance: boolean, msg: Request): Request.AsObject;
  static extensions: {[key: number]: jspb.ExtensionFieldInfo<jspb.Message>};
  static extensionsBinary: {[key: number]: jspb.ExtensionFieldBinaryInfo<jspb.Message>};
  static serializeBinaryToWriter(message: Request, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): Request;
  static deserializeBinaryFromReader(message: Request, reader: jspb.BinaryReader): Request;
}

export namespace Request {
  export type AsObject = {
    revealIntersection: boolean,
    encryptedElementsList: Array<Uint8Array | string>,
  }
}

export class Response extends jspb.Message {
  clearEncryptedElementsList(): void;
  getEncryptedElementsList(): Array<Uint8Array | string>;
  getEncryptedElementsList_asU8(): Array<Uint8Array>;
  getEncryptedElementsList_asB64(): Array<string>;
  setEncryptedElementsList(value: Array<Uint8Array | string>): void;
  addEncryptedElements(value: Uint8Array | string, index?: number): Uint8Array | string;

  serializeBinary(): Uint8Array;
  toObject(includeInstance?: boolean): Response.AsObject;
  static toObject(includeInstance: boolean, msg: Response): Response.AsObject;
  static extensions: {[key: number]: jspb.ExtensionFieldInfo<jspb.Message>};
  static extensionsBinary: {[key: number]: jspb.ExtensionFieldBinaryInfo<jspb.Message>};
  static serializeBinaryToWriter(message: Response, writer: jspb.BinaryWriter): void;
  static deserializeBinary(bytes: Uint8Array): Response;
  static deserializeBinaryFromReader(message: Response, reader: jspb.BinaryReader): Response;
}

export namespace Response {
  export type AsObject = {
    encryptedElementsList: Array<Uint8Array | string>,
  }
}

